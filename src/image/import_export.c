#define _GNU_SOURCE
#include "hermit/image_import_export.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define HERMIT_EXPORT_MAGIC "HERMIT1.0"
#define HERMIT_EXPORT_VERSION 1

struct hermit_export_header {
    char magic[16];
    int version;
    int manifest_size;
    int layer_count;
    size_t total_size;
};

static int compress_file(const char *input_path, const char *output_path)
{
    FILE *input, *output;
    gzFile gz_out;
    char buffer[8192];
    size_t bytes_read;

    input = fopen(input_path, "rb");
    if (!input) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot open input file %s: %s", 
                   input_path, strerror(errno));
        return -1;
    }

    gz_out = gzopen(output_path, "wb");
    if (!gz_out) {
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot create compressed file %s", output_path);
        return -1;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (gzwrite(gz_out, buffer, bytes_read) != (int)bytes_read) {
            fclose(input);
            gzclose(gz_out);
            unlink(output_path);
            return -1;
        }
    }

    fclose(input);
    gzclose(gz_out);
    return 0;
}

static int decompress_file(const char *input_path, const char *output_path)
{
    FILE *output;
    gzFile gz_in;
    char buffer[8192];
    int bytes_read;

    gz_in = gzopen(input_path, "rb");
    if (!gz_in) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot open compressed file %s", input_path);
        return -1;
    }

    output = fopen(output_path, "wb");
    if (!output) {
        gzclose(gz_in);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot create output file %s: %s", 
                   output_path, strerror(errno));
        return -1;
    }

    while ((bytes_read = gzread(gz_in, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, bytes_read, output) != (size_t)bytes_read) {
            fclose(output);
            gzclose(gz_in);
            unlink(output_path);
            return -1;
        }
    }

    fclose(output);
    gzclose(gz_in);
    return 0;
}

int hermit_image_save(const char *image_name, const char *output_file)
{
    struct hermit_image_manifest manifest;
    char manifest_path[PATH_MAX];
    char temp_manifest[PATH_MAX];
    FILE *output;
    struct hermit_export_header header;
    char *manifest_data = NULL;
    size_t manifest_size;
    
    if (!image_name || !output_file) {
        return -1;
    }

    // Load manifest
    if (snprintf(manifest_path, sizeof(manifest_path), 
                 "/tmp/hermitd-state/images/%s.manifest.json", image_name) >= (int)sizeof(manifest_path)) {
        return -1;
    }

    if (hermit_image_parse_manifest(manifest_path, &manifest) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot load manifest for %s", image_name);
        return -1;
    }

    // Read manifest as string
    FILE *manifest_fp = fopen(manifest_path, "r");
    if (!manifest_fp) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot open manifest file: %s", strerror(errno));
        return -1;
    }

    fseek(manifest_fp, 0, SEEK_END);
    manifest_size = ftell(manifest_fp);
    fseek(manifest_fp, 0, SEEK_SET);

    manifest_data = malloc(manifest_size + 1);
    if (!manifest_data) {
        fclose(manifest_fp);
        return -1;
    }

    fread(manifest_data, 1, manifest_size, manifest_fp);
    manifest_data[manifest_size] = '\0';
    fclose(manifest_fp);

    // Create output file
    output = fopen(output_file, "wb");
    if (!output) {
        free(manifest_data);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot create output file: %s", strerror(errno));
        return -1;
    }

    // Write header
    memset(&header, 0, sizeof(header));
    strcpy(header.magic, HERMIT_EXPORT_MAGIC);
    header.version = HERMIT_EXPORT_VERSION;
    header.manifest_size = manifest_size;
    header.layer_count = manifest.layer_count;
    header.total_size = manifest.total_size;

    if (fwrite(&header, sizeof(header), 1, output) != 1) {
        free(manifest_data);
        fclose(output);
        return -1;
    }

    // Write manifest
    if (fwrite(manifest_data, 1, manifest_size, output) != manifest_size) {
        free(manifest_data);
        fclose(output);
        return -1;
    }

    free(manifest_data);

    // Write layers
    for (int i = 0; i < manifest.layer_count; i++) {
        char layer_path[PATH_MAX];
        char temp_comp[PATH_MAX];
        
        if (snprintf(layer_path, sizeof(layer_path), "%s", manifest.layers[i].path) >= (int)sizeof(layer_path)) {
            fclose(output);
            return -1;
        }

        if (snprintf(temp_comp, sizeof(temp_comp), "/tmp/hermit_export_layer_%d.tar.gz", i) >= (int)sizeof(temp_comp)) {
            fclose(output);
            return -1;
        }

        // Compress layer
        if (compress_file(layer_path, temp_comp) != 0) {
            fclose(output);
            return -1;
        }

        // Write compressed layer to output
        FILE *layer_fp = fopen(temp_comp, "rb");
        if (!layer_fp) {
            fclose(output);
            unlink(temp_comp);
            return -1;
        }

        char buffer[8192];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), layer_fp)) > 0) {
            if (fwrite(buffer, 1, bytes_read, output) != bytes_read) {
                fclose(layer_fp);
                fclose(output);
                unlink(temp_comp);
                return -1;
            }
        }

        fclose(layer_fp);
        unlink(temp_comp);
    }

    fclose(output);
    hermit_log(HERMIT_LOG_INFO, "image", "image %s saved to %s", image_name, output_file);
    return 0;
}

int hermit_image_load(const char *input_file)
{
    FILE *input;
    struct hermit_export_header header;
    char *manifest_data = NULL;
    struct hermit_image_manifest manifest;
    
    if (!input_file) {
        return -1;
    }

    input = fopen(input_file, "rb");
    if (!input) {
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot open input file: %s", strerror(errno));
        return -1;
    }

    if (fread(&header, sizeof(header), 1, input) != 1) {
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot read export header");
        return -1;
    }

    if (strcmp(header.magic, HERMIT_EXPORT_MAGIC) != 0 || header.version != HERMIT_EXPORT_VERSION) {
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "invalid export file format");
        return -1;
    }

    manifest_data = malloc(header.manifest_size + 1);
    if (!manifest_data) {
        fclose(input);
        return -1;
    }

    if (fread(manifest_data, 1, header.manifest_size, input) != header.manifest_size) {
        free(manifest_data);
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot read manifest");
        return -1;
    }
    manifest_data[header.manifest_size] = '\0';

    if (hermit_image_parse_manifest_from_string(manifest_data, &manifest) != 0) {
        char image_name[HERMIT_MAX_IMAGE_NAME];
        strcpy(image_name, "imported_image");
        if (snprintf(manifest_data, header.manifest_size + 1, 
                     "{\"name\":\"%s\",\"tag\":\"latest\"}", image_name) > 0) {
            hermit_image_parse_manifest_from_string(manifest_data, &manifest);
        }
    }

    char image_dir[PATH_MAX];
    if (snprintf(image_dir, sizeof(image_dir), "/tmp/hermitd-state/images/%s", manifest.name) >= (int)sizeof(image_dir)) {
        free(manifest_data);
        fclose(input);
        return -1;
    }

    if (mkdir(image_dir, 0755) != 0 && errno != EEXIST) {
        free(manifest_data);
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot create image directory: %s", strerror(errno));
        return -1;
    }

    char manifest_path[PATH_MAX];
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", image_dir) >= (int)sizeof(manifest_path)) {
        free(manifest_data);
        fclose(input);
        return -1;
    }

    FILE *manifest_fp = fopen(manifest_path, "w");
    if (!manifest_fp) {
        free(manifest_data);
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot write manifest: %s", strerror(errno));
        return -1;
    }

    fwrite(manifest_data, 1, header.manifest_size, manifest_fp);
    fclose(manifest_fp);

    char layers_dir[PATH_MAX];
    if (snprintf(layers_dir, sizeof(layers_dir), "%s/layers", image_dir) >= (int)sizeof(layers_dir)) {
        free(manifest_data);
        fclose(input);
        return -1;
    }

    if (mkdir(layers_dir, 0755) != 0 && errno != EEXIST) {
        free(manifest_data);
        fclose(input);
        hermit_log(HERMIT_LOG_ERROR, "image", "cannot create layers directory: %s", strerror(errno));
        return -1;
    }

    for (int i = 0; i < header.layer_count; i++) {
        char temp_comp[PATH_MAX];
        char layer_path[PATH_MAX];
        
        if (snprintf(temp_comp, sizeof(temp_comp), "/tmp/hermit_import_layer_%d.tar.gz", i) >= (int)sizeof(temp_comp)) {
            free(manifest_data);
            fclose(input);
            return -1;
        }

        FILE *temp_fp = fopen(temp_comp, "wb");
        if (!temp_fp) {
            free(manifest_data);
            fclose(input);
            return -1;
        }

        char buffer[8192];
        size_t bytes_read;
        size_t total_read = 0;
        size_t max_layer_size = 100 * 1024 * 1024;
        
        while (total_read < max_layer_size && (bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
            fwrite(buffer, 1, bytes_read, temp_fp);
            total_read += bytes_read;
        }

        fclose(temp_fp);

        if (snprintf(layer_path, sizeof(layer_path), "%s/layer_%d.tar", layers_dir, i) >= (int)sizeof(layer_path)) {
            free(manifest_data);
            fclose(input);
            unlink(temp_comp);
            return -1;
        }

        if (decompress_file(temp_comp, layer_path) != 0) {
            free(manifest_data);
            fclose(input);
            unlink(temp_comp);
            hermit_log(HERMIT_LOG_WARN, "image", "layer %d may be corrupted, skipping", i);
            continue;
        }

        FILE *layer_fp = fopen(layer_path, "rb");
        if (layer_fp) {
            fseek(layer_fp, 0, SEEK_END);
            long size = ftell(layer_fp);
            fseek(layer_fp, 0, SEEK_SET);
            
            char *content = malloc(size);
            if (content) {
                fread(content, 1, size, layer_fp);
                char computed_digest[HERMIT_MAX_DIGEST_LEN];
                hermit_image_calculate_digest(content, size, computed_digest);
                
                if (i < manifest.layer_count && strlen(manifest.layers[i].digest) > 0) {
                    if (strcmp(computed_digest, manifest.layers[i].digest) != 0) {
                        hermit_log(HERMIT_LOG_WARN, "image", "layer %d digest mismatch: expected %s, got %s", 
                                i, manifest.layers[i].digest, computed_digest);
                    }
                }
                free(content);
            }
            fclose(layer_fp);
        }

        unlink(temp_comp);
    }

    free(manifest_data);
    fclose(input);
    
    hermit_log(HERMIT_LOG_INFO, "image", "image loaded from %s as %s", input_file, manifest.name);
    return 0;
}

int hermit_image_export_tar(const char *image_name, const char *output_file)
{
    // Simplified tar export using libarchive
    struct archive *a;
    struct archive_entry *entry;
    char buffer[8192];
    int len;

    a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_add_filter_none(a);
    
    if (archive_write_open_filename(a, output_file, 10240) != ARCHIVE_OK) {
        archive_write_free(a);
        return -1;
    }

    // Add manifest
    char manifest_path[PATH_MAX];
    if (snprintf(manifest_path, sizeof(manifest_path), 
                 "/tmp/hermitd-state/images/%s.manifest.json", image_name) >= (int)sizeof(manifest_path)) {
        archive_write_free(a);
        return -1;
    }

    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "manifest.json");
    archive_entry_set_size(entry, 0); // TODO: Get actual size
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    // TODO: Write actual manifest data
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);
    
    return 0;
}

int hermit_image_import_tar(const char *input_file)
{
    // TODO: Implement tar import using libarchive
    (void)input_file;
    hermit_log(HERMIT_LOG_WARN, "image", "tar import not yet implemented");
    return -1;
}
