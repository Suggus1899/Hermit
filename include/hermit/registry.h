#ifndef HERMIT_REGISTRY_H
#define HERMIT_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_REGISTRY_URL 512
#define HERMIT_MAX_AUTH_TOKEN 2048
#define HERMIT_MAX_IMAGE_REF 256

struct hermit_registry_auth {
    char registry_url[HERMIT_MAX_REGISTRY_URL];
    char username[256];
    char password[256];
    char auth_token[HERMIT_MAX_AUTH_TOKEN];
    time_t token_expires;
    bool authenticated;
};

struct hermit_registry_client {
    struct hermit_registry_auth auth;
    char base_url[HERMIT_MAX_REGISTRY_URL];
    bool insecure;
};

struct hermit_image_manifest {
    char schema_version[16];
    char name[HERMIT_MAX_IMAGE_REF];
    char tag[64];
    char layers[128][HERMIT_MAX_DIGEST_LEN];
    int layer_count;
    char config_digest[HERMIT_MAX_DIGEST_LEN];
};

int hermit_registry_client_init(struct hermit_registry_client *client, const char *registry_url);
int hermit_registry_login(struct hermit_registry_client *client, const char *username, const char *password);
int hermit_registry_logout(struct hermit_registry_client *client);
int hermit_registry_push_image(struct hermit_registry_client *client, const char *image_ref, const char *image_path);
int hermit_registry_pull_image(struct hermit_registry_client *client, const char *image_ref, const char *output_path);
int hermit_registry_verify_digest(const char *data, size_t size, const char *expected_digest);
int hermit_registry_get_manifest(struct hermit_registry_client *client, const char *image_ref, struct hermit_image_manifest *manifest);

#endif
