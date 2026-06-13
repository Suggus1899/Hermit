cmake_minimum_required(VERSION 3.22)
project(Hermit VERSION 1.0.0 LANGUAGES C)

# Opciones de configuración
option(HERMIT_BUILD_TESTS "Build tests" ON)
option(HERMIT_BUILD_TOOLS "Build additional tools" ON)
option(HERMIT_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(HERMIT_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(HERMIT_ENABLE_COVERAGE "Enable code coverage" OFF)
option(HERMIT_ENABLE_STATIC_ANALYSIS "Enable static analysis" OFF)
option(HERMIT_BUILD_FUZZ "Build fuzz tests" OFF)
option(HERMIT_TARGET_WINDOWS "Cross-compile for Windows" OFF)
option(HERMIT_TARGET_MACOS "Cross-compile for macOS" OFF)
option(HERMIT_WSL2_OPTIMIZED "Optimize for WSL2 environment" OFF)

# Configuración de plataforma
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(HERMIT_PLATFORM_WINDOWS TRUE)
    set(HERMIT_PLATFORM_NAME "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HERMIT_PLATFORM_LINUX TRUE)
    set(HERMIT_PLATFORM_NAME "Linux")
    
    # Detectar WSL2
    if(EXISTS "/proc/version")
        file(READ "/proc/version" PROC_VERSION)
        if(PROC_VERSION MATCHES "Microsoft|WSL")
            set(HERMIT_PLATFORM_WSL2 TRUE)
            message(STATUS "Detected WSL2 environment")
        endif()
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(HERMIT_PLATFORM_MACOS TRUE)
    set(HERMIT_PLATFORM_NAME "macOS")
else()
    set(HERMIT_PLATFORM_UNIX TRUE)
    set(HERMIT_PLATFORM_NAME "Unix")
endif()

message(STATUS "Building for platform: ${HERMIT_PLATFORM_NAME}")

# Configuración de compilación
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Flags de compilación por defecto
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -D_GNU_SOURCE")

# Flags de optimización y seguridad
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DNDEBUG")
    
    # Flags de seguridad
    if(HERMIT_PLATFORM_LINUX OR HERMIT_PLATFORM_MACOS)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong -D_FORTIFY_SOURCE=2")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro,-z,now")
    endif()
    
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g3 -DDEBUG")
    
    # Sanitizers en debug
    if(HERMIT_ENABLE_SANITIZERS)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined,leak -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined,leak")
    endif()
    
    # Coverage
    if(HERMIT_ENABLE_COVERAGE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
    
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g -DNDEBUG")
endif()

# WSL2 optimizations
if(HERMIT_WSL2_OPTIMIZED OR HERMIT_PLATFORM_WSL2)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHERMIT_WSL2_OPTIMIZED")
    message(STATUS "WSL2 optimizations enabled")
endif()

# Dependencias requeridas
find_package(PkgConfig REQUIRED)

# OpenSSL
find_package(OpenSSL REQUIRED)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found")
endif()

# libcurl
find_package(CURL REQUIRED)
if(NOT CURL_FOUND)
    message(FATAL_ERROR "libcurl not found")
endif()

# libarchive
find_package(Archive REQUIRED)
if(NOT Archive_FOUND)
    message(FATAL_ERROR "libarchive not found")
endif()

# YAML
find_package(YAML REQUIRED)
if(NOT YAML_FOUND)
    message(FATAL_ERROR "libyaml not found")
endif()

# libcap (Linux only)
if(HERMIT_PLATFORM_LINUX)
    pkg_check_modules(CAP REQUIRED libcap)
    if(NOT CAP_FOUND)
        message(FATAL_ERROR "libcap not found")
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Directorios de fuentes
file(GLOB_RECURSE COMMON_SOURCES 
    "src/common/*.c"
    "src/common/platform/*.c"
)

file(GLOB_RECURSE IMAGE_SOURCES 
    "src/image/*.c"
)

file(GLOB_RECURSE DOCKERFILE_SOURCES 
    "src/dockerfile/*.c"
)

file(GLOB_RECURSE BUILDER_SOURCES 
    "src/builder/*.c"
)

file(GLOB_RECURSE STORAGE_SOURCES 
    "src/storage/*.c"
)

file(GLOB_RECURSE VOLUMES_SOURCES 
    "src/volumes/*.c"
)

file(GLOB_RECURSE REGISTRY_SOURCES 
    "src/registry/*.c"
)

file(GLOB_RECURSE COMPOSE_SOURCES 
    "src/compose/*.c"
)

file(GLOB_RECURSE ORCHESTRATOR_SOURCES 
    "src/orchestrator/*.c"
)

file(GLOB_RECURSE SECURITY_SOURCES 
    "src/security/*.c"
)

file(GLOB_RECURSE OBSERVABILITY_SOURCES 
    "src/observability/*.c"
)

file(GLOB_RECURSE RUNTIME_SOURCES 
    "src/runtime/*.c"
)

file(GLOB_RECURSE DAEMON_SOURCES 
    "src/daemon/*.c"
)

file(GLOB_RECURSE CLI_SOURCES 
    "src/cli/*.c"
)

# Bibliotecas
set(HERMIT_LIBRARIES
    ${COMMON_SOURCES}
    ${IMAGE_SOURCES}
    ${DOCKERFILE_SOURCES}
    ${BUILDER_SOURCES}
    ${STORAGE_SOURCES}
    ${VOLUMES_SOURCES}
    ${REGISTRY_SOURCES}
    ${COMPOSE_SOURCES}
    ${ORCHESTRATOR_SOURCES}
    ${SECURITY_SOURCES}
    ${OBSERVABILITY_SOURCES}
)

# Bibliotecas del sistema
set(HERMIT_SYSTEM_LIBRARIES
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    Archive::Archive
    YAML::YAML
    pthread
    m
)

# libcap solo en Linux
if(HERMIT_PLATFORM_LINUX)
    list(APPEND HERMIT_SYSTEM_LIBRARIES ${CAP_LIBRARIES})
endif()

# Crear biblioteca estática de Hermit
add_library(hermit_static STATIC ${HERMIT_LIBRARIES})
target_link_libraries(hermit_static ${HERMIT_SYSTEM_LIBRARIES})
target_include_directories(hermit_static PUBLIC include)

# Crear biblioteca compartida de Hermit
add_library(hermit_shared SHARED ${HERMIT_LIBRARIES})
target_link_libraries(hermit_shared ${HERMIT_SYSTEM_LIBRARIES})
target_include_directories(hermit_shared PUBLIC include)
set_target_properties(hermit_shared PROPERTIES VERSION ${PROJECT_VERSION})

# Ejecutable hermit (CLI)
add_executable(hermit ${RUNTIME_SOURCES} ${CLI_SOURCES})
target_link_libraries(hermit hermit_static ${HERMIT_SYSTEM_LIBRARIES})

# Ejecutable hermitd (daemon)
add_executable(hermitd ${DAEMON_SOURCES})
target_link_libraries(hermitd hermit_static ${HERMIT_SYSTEM_LIBRARIES})

# Tests
if(HERMIT_BUILD_TESTS)
    enable_testing()
    
    # Framework de tests
    add_executable(hermit_tests
        tests/unit/test_common.c
        tests/unit/test_image.c
        tests/unit/test_dockerfile.c
        tests/unit/test_builder.c
        tests/unit/test_storage.c
        tests/unit/test_volumes.c
        tests/unit/test_registry.c
        tests/unit/test_compose.c
        tests/unit/test_orchestrator.c
        tests/unit/test_security.c
        tests/unit/test_observability.c
        tests/unit/test_runtime.c
    )
    
    target_link_libraries(hermit_tests hermit_static ${HERMIT_SYSTEM_LIBRARIES})
    
    add_test(NAME unit_tests COMMAND hermit_tests)
    
    # Tests de integración
    add_executable(hermit_integration_tests
        tests/integration/test_full_workflow.c
        tests/integration/test_compose_orchestration.c
        tests/integration/test_security_policies.c
    )
    
    target_link_libraries(hermit_integration_tests hermit_static ${HERMIT_SYSTEM_LIBRARIES})
    
    add_test(NAME integration_tests COMMAND hermit_integration_tests)
endif()

# Benchmarks
if(HERMIT_BUILD_BENCHMARKS)
    add_executable(hermit_benchmarks
        tests/benchmarks/benchmark_container_creation.c
        tests/benchmarks/benchmark_image_build.c
        tests/benchmarks/benchmark_network_throughput.c
        tests/benchmarks/benchmark_storage_io.c
    )
    
    target_link_libraries(hermit_benchmarks hermit_static ${HERMIT_SYSTEM_LIBRARIES})
endif()

# Fuzz tests
if(HERMIT_BUILD_FUZZ)
    add_executable(hermit_fuzzer
        tests/fuzz/fuzz_image_parser.c
        tests/fuzz/fuzz_dockerfile_parser.c
        tests/fuzz/fuzz_compose_parser.c
        tests/fuzz/fuzz_registry_client.c
    )
    
    target_link_libraries(hermit_fuzzer hermit_static ${HERMIT_SYSTEM_LIBRARIES})
    
    # Configurar para fuzzing con LLVM
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(hermit_fuzzer PRIVATE -fsanitize=fuzzer)
        target_link_options(hermit_fuzzer PRIVATE -fsanitize=fuzzer)
    endif()
endif()

# Herramientas adicionales
if(HERMIT_BUILD_TOOLS)
    # Herramienta de análisis de seguridad
    add_executable(hermit_security_analyzer
        tools/security_analyzer/main.c
    )
    
    target_link_libraries(hermit_security_analyzer hermit_static ${HERMIT_SYSTEM_LIBRARIES})
    
    # Herramienta de migración desde Docker
    add_executable(hermit_docker_migrator
        tools/docker_migrator/main.c
    )
    
    target_link_libraries(hermit_docker_migrator hermit_static ${HERMIT_SYSTEM_LIBRARIES})
endif()

# Análisis estático
if(HERMIT_ENABLE_STATIC_ANALYSIS)
    find_program(CLANG_TIDY_PATH clang-tidy)
    find_program(CPPCHECK_PATH cppcheck)
    
    if(CLANG_TIDY_PATH)
        set(CMAKE_C_CLANG_TIDY ${CLANG_TIDY_PATH})
        set(CMAKE_C_CLANG_TIDY_CHECKS
            "-*,"
            "performance-*,"
            "readability-*,"
            "bugprone-*,"
            "clang-analyzer-*,"
            "cert-*,"
            "misc-*,"
            "-modernize-use-trailing-return-type"
        )
        
        message(STATUS "Clang-tidy enabled with checks: ${CMAKE_C_CLANG_TIDY_CHECKS}")
    endif()
    
    if(CPPCHECK_PATH)
        add_custom_target(cppcheck
            COMMAND ${CPPCHECK_PATH}
            --enable=all
            --std=c11
            --platform=unix64
            --xml
            --xml-version=2
            --suppressions-list=${CMAKE_SOURCE_DIR}/.cppcheck-suppressions
            ${CMAKE_SOURCE_DIR}/src
            2> cppcheck-report.xml
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running cppcheck static analysis"
        )
        
        add_custom_target(cppcheck-html
            COMMAND ${CPPCHECK_PATH}
            --enable=all
            --std=c11
            --platform=unix64
            --html
            --html-dir=cppcheck-html
            ${CMAKE_SOURCE_DIR}/src
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating cppcheck HTML report"
        )
        
        message(STATUS "Cppcheck enabled")
    endif()
endif()

# Instalación
install(TARGETS hermit hermitd
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

# Scripts de instalación
install(PROGRAMS
    scripts/setup.sh
    scripts/hermit.service
    DESTINATION lib/hermit
)

# Configuración
install(FILES
    configs/hermit.conf
    configs/security-profiles.json
    DESTINATION etc/hermit
)

# Documentación
install(FILES
    README.md
    LICENSE
    docs/USER_GUIDE.md
    docs/SECURITY.md
    DESTINATION share/doc/hermit
)

# Empaquetado
include(CPack)
set(CPACK_PACKAGE_NAME "hermit")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hermit - A lightweight container runtime with Docker-like CLI")
set(CPACK_PACKAGE_VENDOR "Hermit Project")
set(CPACK_PACKAGE_CONTACT "team@hermit.io")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(HERMIT_PLATFORM_LINUX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # Configuración DEB
    set(CPACK_DEBIAN_PACKAGE_SECTION "base")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31), libssl3 (>= 3.0), libcrypto3 (>= 3.0), libarchive13 (>= 3.4), libcurl4 (>= 7.68), libyaml-0-2 (>= 0.2.5)")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Hermit Team <team@hermit.io>")
    
    # Configuración RPM
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.31, openssl >= 3.0, libarchive >= 3.4, libcurl >= 7.68, libyaml >= 0.2.5")
    
elseif(HERMIT_PLATFORM_WINDOWS)
    set(CPACK_GENERATOR "NSIS;ZIP")
    
elseif(HERMIT_PLATFORM_MACOS)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
endif()

# Custom targets
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMENT "Cleaning all build artifacts"
)

add_custom_target(format
    COMMAND find ${CMAKE_SOURCE_DIR}/src -name "*.c" -o -name "*.h" | xargs clang-format -i
    COMMENT "Formatting source code"
)

add_custom_target(check-format
    COMMAND find ${CMAKE_SOURCE_DIR}/src -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror
    COMMENT "Checking code format"
)

# Información de configuración
message(STATUS "")
message(STATUS "Hermit Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${HERMIT_PLATFORM_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "  C Flags: ${CMAKE_C_FLAGS}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Build Tests: ${HERMIT_BUILD_TESTS}")
message(STATUS "  Build Tools: ${HERMIT_BUILD_TOOLS}")
message(STATUS "  Build Benchmarks: ${HERMIT_BUILD_BENCHMARKS}")
message(STATUS "  Enable Sanitizers: ${HERMIT_ENABLE_SANITIZERS}")
message(STATUS "  Enable Coverage: ${HERMIT_ENABLE_COVERAGE}")
message(STATUS "  Enable Static Analysis: ${HERMIT_ENABLE_STATIC_ANALYSIS}")
message(STATUS "  Build Fuzz: ${HERMIT_BUILD_FUZZ}")
message(STATUS "  WSL2 Optimized: ${HERMIT_WSL2_OPTIMIZED}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  OpenSSL: ${OpenSSL_VERSION}")
message(STATUS "  libcurl: ${CURL_VERSION_STRING}")
message(STATUS "  libarchive: ${Archive_VERSION}")
message(STATUS "  libyaml: ${YAML_VERSION_STRING}")
message(STATUS "")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
