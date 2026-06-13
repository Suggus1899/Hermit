#include "hermit/common/fs.h"

#include <stdio.h>
#include <string.h>

static int test_path_join2(void)
{
    char out[64];

    if (hermit_path_join2(out, sizeof(out), "/tmp", "a") != 0) {
        fprintf(stderr, "test_path_join2: hermit_path_join2 failed\n");
        return 1;
    }

    if (strcmp(out, "/tmp/a") != 0) {
        fprintf(stderr, "test_path_join2: unexpected value '%s'\n", out);
        return 1;
    }

    return 0;
}

static int test_sanitize_image_ref(void)
{
    char out[64];

    if (hermit_sanitize_image_ref("my/app:1.0", out, sizeof(out)) != 0) {
        fprintf(stderr, "test_sanitize_image_ref: sanitize failed\n");
        return 1;
    }

    if (strcmp(out, "my_app:1.0") != 0) {
        fprintf(stderr, "test_sanitize_image_ref: unexpected value '%s'\n", out);
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;

    failed |= test_path_join2();
    failed |= test_sanitize_image_ref();

    if (failed) {
        fprintf(stderr, "test_fs: FAILED\n");
        return 1;
    }

    printf("test_fs: OK\n");
    return 0;
}
