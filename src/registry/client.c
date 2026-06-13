#define _GNU_SOURCE
#include "hermit/registry.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    char **response = (char **)userp;
    
    *response = realloc(*response, strlen(*response) + realsize + 1);
    if (*response) {
        strcat(*response, contents);
        (*response)[strlen(*response)] = '\0';
    }
    
    return realsize;
}

static int base64_encode(const char *input, int length, char *output)
{
    static const char encoding_table[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
        'w', 'x', 'y', 'z', '0', '1', '2', '3',
        '4', '5', '6', '7', '8', '9', '+', '/'
    };
    
    int i = 0, j = 0;
    int in_len = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    while (length--) {
        char_array_3[i++] = *(input++);
        in_len++;
        
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                output[j++] = encoding_table[char_array_4[i]];
            }
            
            i = 0;
        }
    }
    
    if (in_len > 0) {
        for (int k = i; k < 3; k++) {
            char_array_3[k] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (int k = 0; k < i + 1; k++) {
            output[j++] = encoding_table[char_array_4[k]];
        }
        
        while ((i++ < 3)) {
            output[j++] = '=';
        }
    }
    
    output[j] = '\0';
    return 0;
}

int hermit_registry_client_init(struct hermit_registry_client *client, const char *registry_url)
{
    if (!client || !registry_url) {
        return -1;
    }
    
    memset(client, 0, sizeof(*client));
    strncpy(client->base_url, registry_url, sizeof(client->base_url) - 1);
    
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    hermit_log(HERMIT_LOG_INFO, "registry", "initialized registry client for %s", registry_url);
    return 0;
}

int hermit_registry_login(struct hermit_registry_client *client, const char *username, const char *password)
{
    CURL *curl;
    CURLcode res;
    char *response = NULL;
    char auth_header[1024];
    char credentials[512];
    char encoded_credentials[1024];
    char url[HERMIT_MAX_REGISTRY_URL];
    
    if (!client || !username || !password) {
        return -1;
    }
    
    // Create basic auth credentials
    snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
    base64_encode(credentials, strlen(credentials), encoded_credentials);
    snprintf(auth_header, sizeof(auth_header), "Basic %s", encoded_credentials);
    
    // Prepare login URL
    snprintf(url, sizeof(url), "%s/v2/auth", client->base_url);
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "login failed: %s", curl_easy_strerror(res));
        free(response);
        return -1;
    }
    
    // Parse response for token (simplified)
    if (response && strstr(response, "\"token\"")) {
        // Extract token from JSON response
        char *token_start = strstr(response, "\"token\":\"");
        if (token_start) {
            token_start += 9; // Skip "token":"
            char *token_end = strstr(token_start, "\"");
            if (token_end) {
                int token_len = token_end - token_start;
                if (token_len < HERMIT_MAX_AUTH_TOKEN) {
                    strncpy(client->auth.auth_token, token_start, token_len);
                    client->auth.auth_token[token_len] = '\0';
                    client->auth.authenticated = true;
                    client->auth.token_expires = time(NULL) + 3600; // 1 hour
                    
                    strncpy(client->auth.username, username, sizeof(client->auth.username) - 1);
                    strncpy(client->auth.password, password, sizeof(client->auth.password) - 1);
                    strncpy(client->auth.registry_url, client->base_url, sizeof(client->auth.registry_url) - 1);
                    
                    hermit_log(HERMIT_LOG_INFO, "registry", "login successful for %s", username);
                    free(response);
                    return 0;
                }
            }
        }
    }
    
    free(response);
    hermit_log(HERMIT_LOG_ERROR, "registry", "login failed: invalid response");
    return -1;
}

int hermit_registry_logout(struct hermit_registry_client *client)
{
    if (!client) {
        return -1;
    }
    
    memset(&client->auth, 0, sizeof(client->auth));
    
    hermit_log(HERMIT_LOG_INFO, "registry", "logged out");
    return 0;
}

int hermit_registry_verify_digest(const char *data, size_t size, const char *expected_digest)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char calculated_digest[HERMIT_MAX_DIGEST_LEN];
    
    if (!data || !expected_digest) {
        return -1;
    }
    
    // Calculate SHA256
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, size);
    SHA256_Final(hash, &sha256);
    
    // Convert to hex string
    strcpy(calculated_digest, "sha256:");
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(calculated_digest + 7 + (i * 2), "%02x", hash[i]);
    }
    
    // Compare digests
    if (strcmp(calculated_digest, expected_digest) == 0) {
        hermit_log(HERMIT_LOG_INFO, "registry", "digest verification successful");
        return 0;
    }
    
    hermit_log(HERMIT_LOG_ERROR, "registry", "digest verification failed: expected %s, got %s", 
               expected_digest, calculated_digest);
    return -1;
}

int hermit_registry_push_image(struct hermit_registry_client *client, const char *image_ref, const char *image_path)
{
    // Simplified push implementation
    char url[HERMIT_MAX_REGISTRY_URL];
    CURL *curl;
    FILE *fp;
    struct stat file_info;
    
    if (!client || !image_ref || !image_path) {
        return -1;
    }
    
    if (!client->auth.authenticated) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "not authenticated");
        return -1;
    }
    
    // Get file info
    if (stat(image_path, &file_info) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "cannot stat image file: %s", strerror(errno));
        return -1;
    }
    
    // Open file
    fp = fopen(image_path, "rb");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "cannot open image file: %s", strerror(errno));
        return -1;
    }
    
    // Prepare upload URL
    snprintf(url, sizeof(url), "%s/v2/images/%s/blobs/uploads/", client->base_url, image_ref);
    
    curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return -1;
    }
    
    // Set auth header
    char auth_header[HERMIT_MAX_AUTH_TOKEN + 50];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", client->auth.auth_token);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "push failed: %s", curl_easy_strerror(res));
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "registry", "pushed image %s", image_ref);
    return 0;
}

int hermit_registry_pull_image(struct hermit_registry_client *client, const char *image_ref, const char *output_path)
{
    // Simplified pull implementation
    char url[HERMIT_MAX_REGISTRY_URL];
    CURL *curl;
    FILE *fp;
    char *response = NULL;
    
    if (!client || !image_ref || !output_path) {
        return -1;
    }
    
    if (!client->auth.authenticated) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "not authenticated");
        return -1;
    }
    
    // Open output file
    fp = fopen(output_path, "wb");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "cannot create output file: %s", strerror(errno));
        return -1;
    }
    
    // Prepare manifest URL
    snprintf(url, sizeof(url), "%s/v2/%s/manifests/latest", client->base_url, image_ref);
    
    curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return -1;
    }
    
    // Set auth header
    char auth_header[HERMIT_MAX_AUTH_TOKEN + 50];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", client->auth.auth_token);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.docker.distribution.manifest.v2+json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fclose(fp);
        hermit_log(HERMIT_LOG_ERROR, "registry", "pull failed: %s", curl_easy_strerror(res));
        free(response);
        return -1;
    }
    
    // For now, just write the manifest response
    if (response) {
        fwrite(response, 1, strlen(response), fp);
        free(response);
    }
    
    fclose(fp);
    
    hermit_log(HERMIT_LOG_INFO, "registry", "pulled image %s to %s", image_ref, output_path);
    return 0;
}

int hermit_registry_get_manifest(struct hermit_registry_client *client, const char *image_ref, struct hermit_image_manifest *manifest)
{
    // Simplified manifest retrieval
    char url[HERMIT_MAX_REGISTRY_URL];
    CURL *curl;
    char *response = NULL;
    
    if (!client || !image_ref || !manifest) {
        return -1;
    }
    
    // Prepare manifest URL
    snprintf(url, sizeof(url), "%s/v2/%s/manifests/latest", client->base_url, image_ref);
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    struct curl_slist *headers = NULL;
    if (client->auth.authenticated) {
        char auth_header[HERMIT_MAX_AUTH_TOKEN + 50];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", client->auth.auth_token);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Accept: application/vnd.docker.distribution.manifest.v2+json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        hermit_log(HERMIT_LOG_ERROR, "registry", "get manifest failed: %s", curl_easy_strerror(res));
        free(response);
        return -1;
    }
    
    // Parse manifest (simplified)
    if (response) {
        memset(manifest, 0, sizeof(*manifest));
        strncpy(manifest->name, image_ref, sizeof(manifest->name) - 1);
        strcpy(manifest->tag, "latest");
        strcpy(manifest->schema_version, "2");
        
        // TODO: Parse actual JSON manifest
        manifest->layer_count = 0;
        
        free(response);
    }
    
    hermit_log(HERMIT_LOG_INFO, "registry", "retrieved manifest for %s", image_ref);
    return 0;
}
