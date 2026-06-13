CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
LDFLAGS := -lssl -lcrypto -larchive -lcurl -lyaml -lz
INCLUDES := -Iinclude

COMMON_SRCS := \
	src/common/error.c \
	src/common/log.c \
	src/common/fs.c

IMAGE_SRCS := \
	src/image/manifest.c \
	src/image/import_export.c

DOCKERFILE_SRCS := \
	src/dockerfile/parser.c \
	src/dockerfile/ignore.c

BUILDER_SRCS := \
	src/builder/layer_builder.c

STORAGE_SRCS := \
	src/storage/overlayfs.c

VOLUMES_SRCS := \
	src/volumes/manager.c

REGISTRY_SRCS := \
	src/registry/client.c \
	src/registry/retry.c

COMPOSE_SRCS := \
	src/compose/parser.c

ORCHESTRATOR_SRCS := \
	src/orchestrator/orchestrator.c

SECURITY_SRCS := \
	src/security/security.c

OBSERVABILITY_SRCS := \
	src/observability/observability.c

RUNTIME_SRCS := \
	src/runtime/run.c

CLI_SRCS := \
	src/cli/main.c \
	src/cli/commands.c

DAEMON_SRCS := \
	src/daemon/main.c \
	src/daemon/server.c

HERMIT_SRCS := $(COMMON_SRCS) $(IMAGE_SRCS) $(DOCKERFILE_SRCS) $(BUILDER_SRCS) $(STORAGE_SRCS) $(VOLUMES_SRCS) $(REGISTRY_SRCS) $(COMPOSE_SRCS) $(ORCHESTRATOR_SRCS) $(SECURITY_SRCS) $(OBSERVABILITY_SRCS) $(RUNTIME_SRCS) $(CLI_SRCS)
HERMITD_SRCS := $(COMMON_SRCS) $(IMAGE_SRCS) $(DOCKERFILE_SRCS) $(BUILDER_SRCS) $(STORAGE_SRCS) $(VOLUMES_SRCS) $(REGISTRY_SRCS) $(COMPOSE_SRCS) $(ORCHESTRATOR_SRCS) $(SECURITY_SRCS) $(OBSERVABILITY_SRCS) $(DAEMON_SRCS)

TARGETS := hermit hermitd
TEST_TARGETS := test_fs

all: $(TARGETS)

hermit: $(HERMIT_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

hermitd: $(HERMITD_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test_fs: src/common/fs.c tests/test_fs.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test_phase2: hermit
	sh tests/test_phase2.sh

test_all_phases: hermit
	sh tests/test_all_phases.sh

test_interrupt_cleanup: hermit
	sh tests/test_interrupt_cleanup.sh

test_init_reaper: hermit
	sh tests/test_init_reaper.sh

test_userns: hermit
	sh tests/test_userns.sh

test_f009_teardown: hermit
	sh tests/test_f009_teardown.sh

test_f017_image_spec: hermit
	sh tests/test_f017_image_spec.sh

test: $(TEST_TARGETS)
	./test_fs
	sh tests/test_phase2.sh
	sh tests/test_all_phases.sh
	sh tests/test_interrupt_cleanup.sh
	sh tests/test_init_reaper.sh
	sh tests/test_userns.sh
	sh tests/test_f009_teardown.sh
	sh tests/test_f017_image_spec.sh

clean:
	rm -f $(TARGETS) $(TEST_TARGETS)

.PHONY: all clean test test_phase2 test_all_phases test_interrupt_cleanup test_init_reaper test_userns test_f009_teardown test_f017_image_spec
