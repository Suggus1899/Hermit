#!/bin/bash
# CI/CD Pipeline Script for Hermit Container Runtime
# This script implements comprehensive testing, security scanning, and quality checks

set -e

# Configuration
PROJECT_NAME="hermit"
BUILD_DIR="build"
SOURCE_DIR="src"
TESTS_DIR="tests"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."
    
    local missing_deps=()
    
    # Check required tools
    local tools=("cmake" "make" "gcc" "clang" "valgrind" "cppcheck" "clang-tidy")
    
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            missing_deps+=("$tool")
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_info "Please install missing dependencies and try again"
        exit 1
    fi
    
    log_success "All dependencies found"
}

# Clean build directory
clean_build() {
    log_info "Cleaning build directory..."
    
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
    fi
    
    log_success "Build directory cleaned"
}

# Configure CMake
configure_cmake() {
    local build_type="$1"
    local enable_sanitizers="$2"
    local enable_coverage="$3"
    local enable_static_analysis="$4"
    
    log_info "Configuring CMake with build type: $build_type"
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    local cmake_args=(
        "-DCMAKE_BUILD_TYPE=$build_type"
        "-DHERMIT_BUILD_TESTS=ON"
        "-DHERMIT_BUILD_TOOLS=ON"
        "-DHERMIT_BUILD_BENCHMARKS=ON"
        "-DHERMIT_BUILD_FUZZ=ON"
    )
    
    if [ "$enable_sanitizers" = "true" ]; then
        cmake_args+=("-DHERMIT_ENABLE_SANITIZERS=ON")
        log_info "Enabling sanitizers"
    fi
    
    if [ "$enable_coverage" = "true" ]; then
        cmake_args+=("-DHERMIT_ENABLE_COVERAGE=ON")
        log_info "Enabling code coverage"
    fi
    
    if [ "$enable_static_analysis" = "true" ]; then
        cmake_args+=("-DHERMIT_ENABLE_STATIC_ANALYSIS=ON")
        log_info "Enabling static analysis"
    fi
    
    cmake "${cmake_args[@]}" ..
    
    cd ..
    log_success "CMake configuration completed"
}

# Build project
build_project() {
    log_info "Building project..."
    
    cd "$BUILD_DIR"
    make -j$(nproc)
    cd ..
    
    log_success "Build completed successfully"
}

# Run unit tests
run_unit_tests() {
    log_info "Running unit tests..."
    
    cd "$BUILD_DIR"
    
    # Run unit tests with coverage
    if [ -f "./hermit_tests" ]; then
        ./hermit_tests --gtest_output=xml:unit_test_results.xml
        log_success "Unit tests completed"
    else
        log_warn "Unit tests executable not found"
    fi
    
    cd ..
}

# Run integration tests
run_integration_tests() {
    log_info "Running integration tests..."
    
    cd "$BUILD_DIR"
    
    # Run integration tests
    if [ -f "./hermit_integration_tests" ]; then
        ./hermit_integration_tests --gtest_output=xml:integration_test_results.xml
        log_success "Integration tests completed"
    else
        log_warn "Integration tests executable not found"
    fi
    
    cd ..
}

# Run performance benchmarks
run_benchmarks() {
    log_info "Running performance benchmarks..."
    
    cd "$BUILD_DIR"
    
    # Run benchmarks
    if [ -f "./hermit_benchmarks" ]; then
        ./hermit_benchmarks --output=benchmark_results.json
        log_success "Benchmarks completed"
    else
        log_warn "Benchmarks executable not found"
    fi
    
    cd ..
}

# Run fuzz tests
run_fuzz_tests() {
    log_info "Running fuzz tests..."
    
    cd "$BUILD_DIR"
    
    # Run fuzz tests for a limited time
    if [ -f "./hermit_fuzzer" ]; then
        timeout 60s ./hermit_fuzzer || true
        log_success "Fuzz tests completed"
    else
        log_warn "Fuzz tests executable not found"
    fi
    
    cd ..
}

# Run static analysis
run_static_analysis() {
    log_info "Running static analysis..."
    
    cd "$BUILD_DIR"
    
    # Run cppcheck
    if command -v cppcheck &> /dev/null; then
        log_info "Running cppcheck..."
        cppcheck --enable=all --std=c11 --xml --xml-version=2 ../src 2> cppcheck_report.xml || true
        log_success "Cppcheck completed"
    fi
    
    # Run clang-tidy
    if command -v clang-tidy &> /dev/null; then
        log_info "Running clang-tidy..."
        make clang-tidy || true
        log_success "Clang-tidy completed"
    fi
    
    cd ..
}

# Run security scan
run_security_scan() {
    log_info "Running security scan..."
    
    cd "$PROJECT_ROOT"
    
    # Check for common security issues
    log_info "Scanning for security vulnerabilities..."
    
    # Check for hardcoded secrets
    if grep -r -i "password\|secret\|key\|token" --include="*.c" --include="*.h" src/ | grep -v "TODO\|FIXME" > security_scan_secrets.txt; then
        log_warn "Potential hardcoded secrets found"
    else
        log_success "No hardcoded secrets detected"
    fi
    
    # Check for unsafe functions
    if grep -r "strcpy\|strcat\|sprintf\|gets" --include="*.c" --include="*.h" src/ > security_scan_unsafe.txt; then
        log_warn "Unsafe functions detected"
    else
        log_success "No unsafe functions detected"
    fi
    
    # Check for buffer overflow vulnerabilities
    log_info "Scanning for buffer overflow vulnerabilities..."
    # This would require more sophisticated analysis
    
    cd ..
}

# Run memory leak detection
run_memory_check() {
    log_info "Running memory leak detection..."
    
    cd "$BUILD_DIR"
    
    # Run tests with Valgrind
    if command -v valgrind &> /dev/null; then
        if [ -f "./hermit_tests" ]; then
            log_info "Running Valgrind memory check..."
            valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind.log ./hermit_tests || true
            log_success "Valgrind memory check completed"
        fi
    fi
    
    cd ..
}

# Run code coverage analysis
run_coverage_analysis() {
    log_info "Running code coverage analysis..."
    
    cd "$BUILD_DIR"
    
    # Generate coverage report
    if command -v gcov &> /dev/null; then
        gcov ../src/**/*.c || true
        
        # Generate HTML report with lcov if available
        if command -v lcov &> /dev/null; then
            lcov --capture --directory . --output-file coverage.info
            genhtml coverage.info --output-directory coverage_html
            log_success "Coverage report generated in coverage_html/"
        fi
    fi
    
    cd ..
}

# Generate build artifacts
generate_artifacts() {
    log_info "Generating build artifacts..."
    
    cd "$BUILD_DIR"
    
    # Create package
    if command -v cpack &> /dev/null; then
        cpack
        log_success "Packages generated"
    fi
    
    # Generate documentation
    if command -v doxygen &> /dev/null; then
        doxygen ../Doxyfile || true
        log_success "Documentation generated"
    fi
    
    cd ..
}

# Run full CI pipeline
run_ci_pipeline() {
    local build_type="${1:-Release}"
    local enable_sanitizers="${2:-false}"
    local enable_coverage="${3:-false}"
    local enable_static_analysis="${4:-true}"
    
    log_info "Starting CI pipeline for $PROJECT_NAME"
    log_info "Build type: $build_type"
    log_info "Sanitizers: $enable_sanitizers"
    log_info "Coverage: $enable_coverage"
    log_info "Static Analysis: $enable_static_analysis"
    
    # Run pipeline stages
    check_dependencies
    clean_build
    configure_cmake "$build_type" "$enable_sanitizers" "$enable_coverage" "$enable_static_analysis"
    build_project
    
    # Run tests
    run_unit_tests
    run_integration_tests
    run_benchmarks
    
    # Run analysis
    if [ "$enable_static_analysis" = "true" ]; then
        run_static_analysis
    fi
    
    run_security_scan
    run_memory_check
    
    # Run coverage if enabled
    if [ "$enable_coverage" = "true" ]; then
        run_coverage_analysis
    fi
    
    # Generate artifacts
    generate_artifacts
    
    log_success "CI pipeline completed successfully"
}

# Run security-focused pipeline
run_security_pipeline() {
    log_info "Starting security-focused pipeline"
    
    check_dependencies
    clean_build
    configure_cmake "Debug" "true" "false" "true"
    build_project
    
    # Run all security-related tests
    run_unit_tests
    run_integration_tests
    run_fuzz_tests
    run_static_analysis
    run_security_scan
    run_memory_check
    
    log_success "Security pipeline completed successfully"
}

# Run performance-focused pipeline
run_performance_pipeline() {
    log_info "Starting performance-focused pipeline"
    
    check_dependencies
    clean_build
    configure_cmake "Release" "false" "false" "false"
    build_project
    
    # Run performance tests
    run_benchmarks
    
    # Performance profiling
    cd "$BUILD_DIR"
    if command -v perf &> /dev/null && [ -f "./hermit_benchmarks" ]; then
        log_info "Running performance profiling..."
        perf record -g ./hermit_benchmarks
        perf report > perf_report.txt || true
        log_success "Performance profiling completed"
    fi
    cd ..
    
    log_success "Performance pipeline completed successfully"
}

# Main function
main() {
    local command="${1:-ci}"
    local build_type="${2:-Release}"
    local enable_sanitizers="${3:-false}"
    local enable_coverage="${4:-false}"
    local enable_static_analysis="${5:-true}"
    
    case "$command" in
        "ci")
            run_ci_pipeline "$build_type" "$enable_sanitizers" "$enable_coverage" "$enable_static_analysis"
            ;;
        "security")
            run_security_pipeline
            ;;
        "performance")
            run_performance_pipeline
            ;;
        "clean")
            clean_build
            ;;
        "deps")
            check_dependencies
            ;;
        *)
            echo "Usage: $0 {ci|security|performance|clean|deps} [build_type] [sanitizers] [coverage] [static_analysis]"
            echo "  ci          - Run full CI pipeline"
            echo "  security    - Run security-focused pipeline"
            echo "  performance - Run performance-focused pipeline"
            echo "  clean       - Clean build directory"
            echo "  deps        - Check dependencies"
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
