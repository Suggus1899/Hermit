#define _GNU_SOURCE
#include "hermit/builder.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int calculate_context_hash(const char *context_path, char *hash_out)
{
    DIR *dir;
    struct dirent *entry;
    SHA256_CTX sha256;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char file_path[PATH_MAX];
    
    SHA256_Init(&sha256);
    
    dir = opendir(context_path);
    if (!dir) {
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(file_path, sizeof(file_path), "%s/%s", context_path, entry->d_name);
        
        // Add file name to hash
        SHA256_Update(&sha256, entry->d_name, strlen(entry->d_name));
        
        // Add file content if it's a regular file
        struct stat st;
        if (stat(file_path, &st) == 0 && S_ISREG(st.st_mode)) {
            FILE *fp = fopen(file_path, "rb");
            if (fp) {
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                    SHA256_Update(&sha256, buffer, bytes_read);
                }
                fclose(fp);
            }
        }
    }
    
    closedir(dir);
    SHA256_Final(hash, &sha256);
    
    // Convert to hex string
    strcpy(hash_out, "sha256:");
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash_out + 7 + (i * 2), "%02x", hash[i]);
    }
    
    return 0;
}

static int copy_directory_recursive(const char *src_path, const char *dst_path)
{
    DIR *dir;
    struct dirent *ent;
    char src_file[PATH_MAX];
    char dst_file[PATH_MAX];
    
    if (mkdir(dst_path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    dir = opendir(src_path);
    if (!dir) {
        return -1;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(src_file, sizeof(src_file), "%s/%s", src_path, ent->d_name);
        snprintf(dst_file, sizeof(dst_file), "%s/%s", dst_path, ent->d_name);
        
        struct stat st;
        if (stat(src_file, &st) != 0) {
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            copy_directory_recursive(src_file, dst_file);
        } else if (S_ISREG(st.st_mode)) {
            FILE *src = fopen(src_file, "rb");
            if (src) {
                FILE *dst = fopen(dst_file, "wb");
                if (dst) {
                    char buffer[8192];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                        fwrite(buffer, 1, bytes, dst);
                    }
                    fclose(dst);
                    chmod(dst_file, st.st_mode);
                }
                fclose(src);
            }
        }
    }
    
    closedir(dir);
    return 0;
}

static int create_tar_archive(const char *source_dir, const char *output_tar)
{
    struct archive *a;
    struct archive_entry *entry;
    char buffer[8192];
    int len;
    
    a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_add_filter_none(a);
    
    if (archive_write_open_filename(a, output_tar, 10240) != ARCHIVE_OK) {
        archive_write_free(a);
        return -1;
    }
    
    // Add directory contents to tar
    DIR *dir = opendir(source_dir);
    if (!dir) {
        archive_write_free(a);
        return -1;
    }
    
    struct dirent *entry_info;
    while ((entry_info = readdir(dir)) != NULL) {
        if (strcmp(entry_info->d_name, ".") == 0 || strcmp(entry_info->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", source_dir, entry_info->d_name);
        
        entry = archive_entry_new();
        archive_entry_set_pathname(entry, entry_info->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            archive_entry_set_size(entry, st.st_size);
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
        }
        
        archive_write_header(a, entry);
        
        // Write file content
        FILE *fp = fopen(full_path, "rb");
        if (fp) {
            while ((len = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                archive_write_data(a, buffer, len);
            }
            fclose(fp);
        }
        
        archive_entry_free(entry);
    }
    
    closedir(dir);
    archive_write_close(a);
    archive_write_free(a);
    
    return 0;
}

int hermit_builder_generate_cache_key(const struct hermit_build_step *step, const char *context_hash, char *cache_key)
{
    char step_content[1024];
    
    // Create step content string
    strcpy(step_content, hermit_instruction_type_to_string(step->type));
    for (int i = 0; i < step->arg_count; i++) {
        strcat(step_content, " ");
        strcat(step_content, step->args[i]);
    }
    
    // Calculate combined hash
    SHA256_CTX sha256;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, context_hash, strlen(context_hash));
    SHA256_Update(&sha256, step_content, strlen(step_content));
    SHA256_Final(hash, &sha256);
    
    // Convert to hex string
    strcpy(cache_key, "sha256:");
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(cache_key + 7 + (i * 2), "%02x", hash[i]);
    }
    
    return 0;
}

int hermit_builder_check_cache(const char *cache_key, char *layer_digest)
{
    char cache_path[PATH_MAX];
    FILE *fp;
    
    snprintf(cache_path, sizeof(cache_path), "/tmp/hermitd-state/cache/%s", cache_key + 7);
    
    fp = fopen(cache_path, "r");
    if (!fp) {
        return -1; // Cache miss
    }
    
    if (fscanf(fp, "%255s", layer_digest) != 1) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return 0; // Cache hit
}

int hermit_builder_init(struct hermit_build_context *ctx, const char *image_name, 
                     const char *tag, const char *context_path, const char *dockerfile_path)
{
    if (!ctx || !image_name || !tag || !context_path || !dockerfile_path) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(*ctx));
    
    ctx->image_name = strdup(image_name);
    ctx->tag = strdup(tag);
    ctx->context_path = strdup(context_path);
    ctx->dockerfile_path = strdup(dockerfile_path);
    
    if (!ctx->image_name || !ctx->tag || !ctx->context_path || !ctx->dockerfile_path) {
        hermit_builder_cleanup(ctx);
        return -1;
    }
    
    // Parse dockerfile
    if (hermit_parse_dockerfile(dockerfile_path, &ctx->dockerfile) != 0) {
        hermit_builder_cleanup(ctx);
        return -1;
    }
    
    // Validate dockerfile
    if (hermit_validate_dockerfile(&ctx->dockerfile) != 0) {
        hermit_builder_cleanup(ctx);
        return -1;
    }
    
    // Initialize manifest
    strcpy(ctx->manifest.schema_version, HERMIT_IMAGE_MANIFEST_VERSION);
    strcpy(ctx->manifest.name, image_name);
    strcpy(ctx->manifest.tag, tag);
    strcpy(ctx->manifest.config.architecture, "amd64");
    strcpy(ctx->manifest.config.os, "linux");
    
    // Create build directories
    mkdir("/tmp/hermitd-state/cache", 0755);
    mkdir("/tmp/hermit_build_context", 0755);
    
    hermit_log(HERMIT_LOG_INFO, "builder", "initialized build for %s:%s", image_name, tag);
    return 0;
}

int hermit_builder_create_layer(struct hermit_build_context *ctx, struct hermit_build_step *step)
{
    char layer_dir[PATH_MAX];
    char tar_path[PATH_MAX];
    char digest[HERMIT_MAX_DIGEST_LEN];
    
    snprintf(layer_dir, sizeof(layer_dir), "%s/layer_%d", HERMIT_BUILD_CONTEXT_DIR, step - ctx->steps);
    if (mkdir(layer_dir, 0755) != 0) {
        return -1;
    }
    
    switch (step->type) {
        case HERMIT_INSTRUCTION_COPY:
            if (step->arg_count >= 2) {
                char src_path[PATH_MAX];
                char dst_path[PATH_MAX];
                
                snprintf(src_path, sizeof(src_path), "%s/%s", ctx->context_path, step->args[0]);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", layer_dir, step->args[1]);
                
                struct stat st;
                if (stat(src_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    copy_directory_recursive(src_path, dst_path);
                } else {
                    FILE *src = fopen(src_path, "rb");
                    if (src) {
                        FILE *dst = fopen(dst_path, "wb");
                        if (dst) {
                            char buffer[8192];
                            size_t bytes;
                            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                                fwrite(buffer, 1, bytes, dst);
                            }
                            fclose(dst);
                        }
                        fclose(src);
                    }
                }
            }
            break;
            
        case HERMIT_INSTRUCTION_RUN:
            if (step->arg_count > 0) {
                char command[1024];
                strcpy(command, step->args[0]);
                for (int i = 1; i < step->arg_count; i++) {
                    strcat(command, " ");
                    strcat(command, step->args[i]);
                }
                
                char cmd_output[8192];
                snprintf(cmd_output, sizeof(cmd_output), "cd %s && %s > command_output.txt 2>&1", layer_dir, command);
                system(cmd_output);
            }
            break;
            
        case HERMIT_INSTRUCTION_WORKDIR:
            if (step->arg_count > 0) {
                char workdir_path[PATH_MAX];
                snprintf(workdir_path, sizeof(workdir_path), "%s/%s", layer_dir, step->args[0]);
                mkdir(workdir_path, 0755);
            }
            break;
            
        case HERMIT_INSTRUCTION_ENV:
            if (step->arg_count >= 2) {
                // ENV instruction - set environment variable
                char env_line[512];
                snprintf(env_line, sizeof(env_line), "export %s=%s", step->args[0], step->args[1]);
                // Store for later application
            }
            break;
            
        case HERMIT_INSTRUCTION_EXPOSE:
            // Store port for later - just metadata
            break;
            
        default:
            break;
    }
    
    snprintf(tar_path, sizeof(tar_path), "%s.tar", layer_dir);
    if (create_tar_archive(layer_dir, tar_path) != 0) {
        return -1;
    }
    
    FILE *tar_fp = fopen(tar_path, "rb");
    if (tar_fp) {
        fseek(tar_fp, 0, SEEK_END);
        long size = ftell(tar_fp);
        fseek(tar_fp, 0, SEEK_SET);
        
        char *content = malloc(size);
        if (content) {
            fread(content, 1, size, tar_fp);
            hermit_image_calculate_digest(content, size, digest);
            free(content);
        }
        fclose(tar_fp);
    }
    
    char compressed_path[PATH_MAX];
    snprintf(compressed_path, sizeof(compressed_path), "%s.tar.gz", tar_path);
    compress_file(tar_path, compressed_path);
    
    strcpy(step->layer_digest, digest);
    step->completed = true;
    
    if (ctx->manifest.layer_count < HERMIT_MAX_LAYERS) {
        struct hermit_image_layer *layer = &ctx->manifest.layers[ctx->manifest.layer_count];
        strcpy(layer->digest, digest);
        strcpy(layer->media_type, "application/vnd.hermit.layer.tar+gzip");
        layer->size = 0;
        strcpy(layer->path, compressed_path);
        ctx->manifest.layer_count++;
    }
    
    hermit_log(HERMIT_LOG_INFO, "builder", "created layer %s", digest);
    return 0;
}

int hermit_builder_execute(struct hermit_build_context *ctx)
{
    char context_hash[HERMIT_MAX_DIGEST_LEN];
    
    if (!ctx) {
        return -1;
    }
    
    // Calculate context hash for cache keys
    calculate_context_hash(ctx->context_path, context_hash);
    
    hermit_log(HERMIT_LOG_INFO, "builder", "starting build with %d steps", ctx->dockerfile.instruction_count);
    
    // Convert dockerfile instructions to build steps
    for (int i = 0; i < ctx->dockerfile.instruction_count && ctx->step_count < HERMIT_MAX_BUILD_STEPS; i++) {
        struct hermit_instruction *instr = &ctx->dockerfile.instructions[i];
        struct hermit_build_step *step = &ctx->steps[ctx->step_count];
        
        step->type = instr->type;
        step->arg_count = instr->arg_count;
        for (int j = 0; j < instr->arg_count; j++) {
            step->args[j] = strdup(instr->args[j]);
        }
        
        // Generate cache key and check cache
        hermit_builder_generate_cache_key(step, context_hash, step->cache_key);
        if (hermit_builder_check_cache(step->cache_key, step->layer_digest) == 0) {
            step->cached = true;
            hermit_log(HERMIT_LOG_INFO, "builder", "cache hit for step %d", i);
        } else {
            step->cached = false;
            hermit_builder_create_layer(ctx, step);
            
            // Store in cache
            char cache_path[PATH_MAX];
            snprintf(cache_path, sizeof(cache_path), "/tmp/hermitd-state/cache/%s", step->cache_key + 7);
            FILE *cache_fp = fopen(cache_path, "w");
            if (cache_fp) {
                fprintf(cache_fp, "%s", step->layer_digest);
                fclose(cache_fp);
            }
        }
        
        ctx->step_count++;
    }
    
    // Write manifest
    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "/tmp/hermitd-state/images/%s:%s.manifest.json", 
             ctx->image_name, ctx->tag);
    hermit_image_write_manifest(manifest_path, &ctx->manifest);
    
    hermit_log(HERMIT_LOG_INFO, "builder", "build completed successfully");
    return 0;
}

int hermit_builder_cleanup(struct hermit_build_context *ctx)
{
    if (!ctx) return 0;
    
    free(ctx->image_name);
    free(ctx->tag);
    free(ctx->context_path);
    free(ctx->dockerfile_path);
    
    hermit_free_dockerfile(&ctx->dockerfile);
    
    // Free build steps
    for (int i = 0; i < ctx->step_count; i++) {
        for (int j = 0; j < ctx->steps[i].arg_count; j++) {
            free(ctx->steps[i].args[j]);
        }
    }
    
    // Cleanup temporary directories
    char cmd[PATH_MAX];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", HERMIT_BUILD_CONTEXT_DIR);
    system(cmd);
    
    return 0;
}
