#define _GNU_SOURCE
#include "hermit/image.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *valid_name_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";

int hermit_image_validate_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    size_t len = strlen(name);
    if (len >= HERMIT_MAX_IMAGE_NAME) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        if (strchr(valid_name_chars, name[i]) == NULL) {
            return -1;
        }
    }

    return 0;
}

int hermit_image_validate_tag(const char *tag)
{
    if (tag == NULL || tag[0] == '\0') {
        return -1;
    }

    size_t len = strlen(tag);
    if (len >= HERMIT_MAX_TAG_LEN) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        if (strchr(valid_name_chars, tag[i]) == NULL) {
            return -1;
        }
    }

    return 0;
}

int hermit_image_calculate_digest(const char *data, size_t len, char *digest_out)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    
    if (data == NULL || digest_out == NULL) {
        return -1;
    }

    SHA256_CTX sha256;
    if (SHA256_Init(&sha256) != 1) {
        return -1;
    }

    if (SHA256_Update(&sha256, data, len) != 1) {
        return -1;
    }

    if (SHA256_Final(hash, &sha256) != 1) {
        return -1;
    }

    // Convert to hex string with "sha256:" prefix
    strcpy(digest_out, "sha256:");
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(digest_out + 7 + (i * 2), "%02x", hash[i]);
    }

    return 0;
}

int hermit_image_get_layer_path(const char *image_name, const char *digest, char *path_out, size_t path_size)
{
    if (image_name == NULL || digest == NULL || path_out == NULL) {
        return -1;
    }

    if (snprintf(path_out, path_size, "/tmp/hermitd-state/images/%s/layers/%s.tar.gz", 
                 image_name, digest + 7) >= (int)path_size) {
        return -1;
    }

    return 0;
}

static char* escape_json_string(const char *str)
{
    if (str == NULL) {
        return strdup("null");
    }

    size_t len = strlen(str);
    char *escaped = malloc(len * 2 + 3); // Worst case: all chars escaped + quotes
    if (escaped == NULL) {
        return NULL;
    }

    size_t j = 0;
    escaped[j++] = '"';
    
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                escaped[j++] = str[i];
                break;
        }
    }
    
    escaped[j++] = '"';
    escaped[j] = '\0';
    
    return escaped;
}

int hermit_image_write_manifest(const char *json_path, const struct hermit_image_manifest *manifest)
{
    FILE *fp;
    char *escaped_str;
    
    if (json_path == NULL || manifest == NULL) {
        return -1;
    }

    fp = fopen(json_path, "w");
    if (fp == NULL) {
        return -1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schemaVersion\": %s,\n", manifest->schema_version);
    
    escaped_str = escape_json_string(manifest->name);
    fprintf(fp, "  \"name\": %s,\n", escaped_str);
    free(escaped_str);
    
    escaped_str = escape_json_string(manifest->tag);
    fprintf(fp, "  \"tag\": %s,\n", escaped_str);
    free(escaped_str);
    
    escaped_str = escape_json_string(manifest->created);
    fprintf(fp, "  \"created\": %s,\n", escaped_str);
    free(escaped_str);

    // Write config
    fprintf(fp, "  \"config\": {\n");
    fprintf(fp, "    \"architecture\": \"%s\",\n", manifest->config.architecture);
    fprintf(fp, "    \"os\": \"%s\",\n", manifest->config.os);
    
    escaped_str = escape_json_string(manifest->config.working_dir);
    fprintf(fp, "    \"WorkingDir\": %s,\n", escaped_str);
    free(escaped_str);
    
    escaped_str = escape_json_string(manifest->config.user);
    fprintf(fp, "    \"User\": %s,\n", escaped_str);
    free(escaped_str);
    
    fprintf(fp, "    \"StopSignal\": %d,\n", manifest->config.stop_signal);

    // Env vars
    fprintf(fp, "    \"Env\": [");
    for (int i = 0; i < manifest->config.env_count; i++) {
        if (i > 0) fprintf(fp, ", ");
        escaped_str = escape_json_string(manifest->config.env_vars[i]);
        fprintf(fp, "%s", escaped_str);
        free(escaped_str);
    }
    fprintf(fp, "],\n");

    // Cmd
    fprintf(fp, "    \"Cmd\": [");
    for (int i = 0; i < manifest->config.cmd_count; i++) {
        if (i > 0) fprintf(fp, ", ");
        escaped_str = escape_json_string(manifest->config.cmd[i]);
        fprintf(fp, "%s", escaped_str);
        free(escaped_str);
    }
    fprintf(fp, "],\n");

    // Entrypoint
    fprintf(fp, "    \"Entrypoint\": [");
    for (int i = 0; i < manifest->config.entrypoint_count; i++) {
        if (i > 0) fprintf(fp, ", ");
        escaped_str = escape_json_string(manifest->config.entrypoint[i]);
        fprintf(fp, "%s", escaped_str);
        free(escaped_str);
    }
    fprintf(fp, "],\n");

    // Labels
    fprintf(fp, "    \"Labels\": {");
    for (int i = 0; i < manifest->config.label_count; i += 2) {
        if (i > 0) fprintf(fp, ", ");
        escaped_str = escape_json_string(manifest->config.labels[i]);
        fprintf(fp, "%s", escaped_str);
        free(escaped_str);
        fprintf(fp, ": ");
        escaped_str = escape_json_string(manifest->config.labels[i + 1]);
        fprintf(fp, "%s", escaped_str);
        free(escaped_str);
    }
    fprintf(fp, "}\n");

    fprintf(fp, "  },\n");

    // Layers
    fprintf(fp, "  \"layers\": [\n");
    for (int i = 0; i < manifest->layer_count; i++) {
        if (i > 0) fprintf(fp, ",\n");
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"digest\": \"%s\",\n", manifest->layers[i].digest);
        fprintf(fp, "      \"mediaType\": \"%s\",\n", manifest->layers[i].media_type);
        fprintf(fp, "      \"size\": %zu,\n", manifest->layers[i].size);
        escaped_str = escape_json_string(manifest->layers[i].path);
        fprintf(fp, "      \"path\": %s\n", escaped_str);
        free(escaped_str);
        fprintf(fp, "    }");
    }
    fprintf(fp, "\n  ],\n");

    fprintf(fp, "  \"layerCount\": %d,\n", manifest->layer_count);
    fprintf(fp, "  \"totalSize\": %zu\n", manifest->total_size);
    fprintf(fp, "}\n");

    if (fclose(fp) != 0) {
        return -1;
    }

    return 0;
}

int hermit_image_parse_manifest(const char *json_path, struct hermit_image_manifest *manifest)
{
    FILE *fp;
    char buffer[8192];
    size_t len;
    char *json_content;
    
    if (json_path == NULL || manifest == NULL) {
        return -1;
    }
    
    fp = fopen(json_path, "r");
    if (fp == NULL) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot open manifest %s: %s", 
                   json_path, strerror(errno));
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    json_content = malloc(len + 1);
    if (json_content == NULL) {
        fclose(fp);
        return -1;
    }
    
    fread(json_content, 1, len, fp);
    json_content[len] = '\0';
    fclose(fp);
    
    int result = hermit_image_parse_manifest_from_string(json_content, manifest);
    free(json_content);
    return result;
}

int hermit_image_parse_manifest_from_string(const char *json_str, struct hermit_image_manifest *manifest)
{
    char *ptr;
    char *json_content;
    size_t len;
    char *end;
    
    if (json_str == NULL || manifest == NULL) {
        return -1;
    }
    
    len = strlen(json_str);
    json_content = malloc(len + 1);
    if (json_content == NULL) {
        return -1;
    }
    strcpy(json_content, json_str);
    
    memset(manifest, 0, sizeof(*manifest));
    
    ptr = json_content;
    end = json_content + len;
    
    while (ptr < end) {
        if (strncmp(ptr, "\"schemaVersion\"", 12) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ' || *ptr == '"') ptr++;
            char *quote_end = strchr(ptr, '"');
            if (quote_end) {
                size_t copy_len = quote_end - ptr;
                if (copy_len >= sizeof(manifest->schema_version)) copy_len = sizeof(manifest->schema_version) - 1;
                strncpy(manifest->schema_version, ptr, copy_len);
                manifest->schema_version[copy_len] = '\0';
            }
        } else if (strncmp(ptr, "\"name\"", 5) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ' || *ptr == '"') ptr++;
            char *quote_end = strchr(ptr, '"');
            if (quote_end) {
                size_t copy_len = quote_end - ptr;
                if (copy_len >= HERMIT_MAX_IMAGE_NAME) copy_len = HERMIT_MAX_IMAGE_NAME - 1;
                strncpy(manifest->name, ptr, copy_len);
                manifest->name[copy_len] = '\0';
            }
        } else if (strncmp(ptr, "\"tag\"", 4) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ' || *ptr == '"') ptr++;
            char *quote_end = strchr(ptr, '"');
            if (quote_end) {
                size_t copy_len = quote_end - ptr;
                if (copy_len >= HERMIT_MAX_TAG_LEN) copy_len = HERMIT_MAX_TAG_LEN - 1;
                strncpy(manifest->tag, ptr, copy_len);
                manifest->tag[copy_len] = '\0';
            }
        } else if (strncmp(ptr, "\"created\"", 7) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ' || *ptr == '"') ptr++;
            char *quote_end = strchr(ptr, '"');
            if (quote_end) {
                size_t copy_len = quote_end - ptr;
                if (copy_len >= 64) copy_len = 63;
                strncpy(manifest->created, ptr, copy_len);
                manifest->created[copy_len] = '\0';
            }
        } else if (strncmp(ptr, "\"layerCount\"", 10) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ') ptr++;
            manifest->layer_count = atoi(ptr);
        } else if (strncmp(ptr, "\"totalSize\"", 8) == 0) {
            ptr = strchr(ptr, ':') + 1;
            while (*ptr == ' ') ptr++;
            manifest->total_size = atol(ptr);
        }
        
        ptr = strchr(ptr, '\n');
        if (ptr == NULL) break;
        ptr++;
    }
    
    free(json_content);
    hermit_log(HERMIT_LOG_DEBUG, "image", "parsed manifest for %s:%s (%d layers)", 
               manifest->name, manifest->tag, manifest->layer_count);
    return 0;
}
