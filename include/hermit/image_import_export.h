#ifndef HERMIT_IMAGE_IMPORT_EXPORT_H
#define HERMIT_IMAGE_IMPORT_EXPORT_H

#include "hermit/image.h"
#include <stddef.h>

int hermit_image_save(const char *image_name, const char *output_file);
int hermit_image_load(const char *input_file);
int hermit_image_export_tar(const char *image_name, const char *output_file);
int hermit_image_import_tar(const char *input_file);

#endif
