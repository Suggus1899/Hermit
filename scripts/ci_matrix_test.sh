#!/bin/bash
# CI Matrix Test Script for Hermit
# Tests multiple kernel distributions and configurations

set -e

HERMIT_BIN="./hermit"
HERMITD_BIN="./hermitd"
TEST_LOG="/tmp/hermit_ci_test.log"
TEST_RESULTS="/tmp/hermit_ci_results.json"

# Test configurations
declare -A TEST_CONFIGS=(
    ["ubuntu"]="Ubuntu 22.04"
    ["alpine"]="Alpine 3.18"
    ["busybox"]="BusyBox 1.36"
    ["debian"]="Debian 12"
)

# Initialize results
echo '{"test_results":[' > "$TEST_RESULTS"

test_count=0
total_tests=0

# Function to run a single test
run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected_result="$3"
    
    echo "Running test: $test_name" | tee -a "$TEST_LOG"
    
    if eval "$test_command" >> "$TEST_LOG" 2>&1; then
        if [ "$expected_result" = "success" ]; then
            echo "  PASS: $test_name" | tee -a "$TEST_LOG"
            echo '  {"name": "'$test_name'", "status": "pass"},' >> "$TEST_RESULTS"
        else
            echo "  FAIL: $test_name (expected failure but passed)" | tee -a "$TEST_LOG"
            echo '  {"name": "'$test_name'", "status": "fail", "reason": "expected failure but passed"},' >> "$TEST_RESULTS"
        fi
    else
        if [ "$expected_result" = "failure" ]; then
            echo "  PASS: $test_name (expected failure)" | tee -a "$TEST_LOG"
            echo '  {"name": "'$test_name'", "status": "pass"},' >> "$TEST_RESULTS"
        else
            echo "  FAIL: $test_name" | tee -a "$TEST_LOG"
            echo '  {"name": "'$test_name'", "status": "fail", "reason": "command failed"},' >> "$TEST_RESULTS"
        fi
    fi
    
    test_count=$((test_count + 1))
    total_tests=$((total_tests + 1))
}

# Function to test build with different base images
test_build_base_images() {
    echo "Testing build with different base images..." | tee -a "$TEST_LOG"
    
    for distro in "${!TEST_CONFIGS[@]}"; do
        echo "Testing $distro base image..." | tee -a "$TEST_LOG"
        
        # Create test Dockerfile
        cat > "Dockerfile.$distro" << EOF
FROM ${distro}:latest
RUN echo "Hello from $distro" > /test.txt
CMD ["cat", "/test.txt"]
EOF
        
        # Test build
        run_test "build_$distro" "$HERMIT_BIN build -t test-$distro:latest -f Dockerfile.$distro ." "success"
        
        # Clean up
        rm -f "Dockerfile.$distro" 2>/dev/null || true
    done
}

# Function to test runtime features
test_runtime_features() {
    echo "Testing runtime features..." | tee -a "$TEST_LOG"
    
    # Test basic container execution
    run_test "runtime_basic" "$HERMIT_BIN run --rm echo 'Hello World'" "success"
    
    # Test namespaces
    run_test "runtime_namespaces" "$HERMIT_BIN run --rm --hostname test-container echo 'test'" "success"
    
    # Test resource limits
    run_test "runtime_limits" "$HERMIT_BIN run --rm --cpu-max '50000 100000' --memory-max 134217728 echo 'test'" "success"
    
    # Test networking
    run_test "runtime_networking" "$HERMIT_BIN run --rm --net-veth --net-nat --out-if lo echo 'test'" "success"
    
    # Test user namespaces
    run_test "runtime_userns" "$HERMIT_BIN run --rm --userns echo 'test'" "success"
}

# Function to test daemon functionality
test_daemon_functionality() {
    echo "Testing daemon functionality..." | tee -a "$TEST_LOG"
    
    # Start daemon
    $HERMITD_BIN &
    DAEMON_PID=$!
    sleep 2
    
    # Test daemon communication
    run_test "daemon_ping" "echo 'PING' | nc -U /tmp/hermit.sock" "success"
    
    # Test container management
    run_test "daemon_container_create" "$HERMIT_BIN container create test-container" "success"
    run_test "daemon_container_start" "$HERMIT_BIN container start test-container" "success"
    run_test "daemon_container_stop" "$HERMIT_BIN container stop test-container" "success"
    run_test "daemon_container_rm" "$HERMIT_BIN container rm test-container" "success"
    
    # Stop daemon
    kill $DAEMON_PID
    wait $DAEMON_PID 2>/dev/null || true
}

# Function to test security features
test_security_features() {
    echo "Testing security features..." | tee -a "$TEST_LOG"
    
    # Test with security profile
    run_test "security_default" "$HERMIT_BIN run --rm --security-profile default echo 'test'" "success"
    
    # Test no new privileges
    run_test "security_no_new_privs" "$HERMIT_BIN run --rm --no-new-privileges echo 'test'" "success"
    
    # Test capability dropping
    run_test "security_drop_caps" "$HERMIT_BIN run --rm --drop-all-caps echo 'test'" "success"
}

# Function to test observability
test_observability() {
    echo "Testing observability..." | tee -a "$TEST_LOG"
    
    # Test metrics collection
    run_test "observability_metrics" "$HERMIT_BIN run --rm --enable-metrics echo 'test'" "success"
    
    # Test structured logging
    run_test "observability_logging" "$HERMIT_BIN run --rm --structured-logs echo 'test'" "success"
    
    # Test event streaming
    run_test "observability_events" "$HERMIT_BIN run --rm --enable-events echo 'test'" "success"
}

# Function to test memory safety
test_memory_safety() {
    echo "Testing memory safety (ASan/UBSan)..." | tee -a "$TEST_LOG"
    
    # Build with sanitizers
    make clean
    CFLAGS="-fsanitize=address,undefined -g" make hermit
    
    # Run basic tests with sanitized binary
    run_test "memory_basic" "./hermit run --rm echo 'Hello World'" "success"
    
    # Restore normal build
    make clean
    make hermit
}

# Function to test packaging
test_packaging() {
    echo "Testing packaging..." | tee -a "$TEST_LOG"
    
    # Test tar.gz package
    run_test "package_tar" "tar -czf hermit-1.0.0.tar.gz hermit hermitd" "success"
    
    # Test deb package (if available)
    if command -v dpkg-deb; then
        run_test "package_deb" "dpkg-deb --build hermit-1.0.0" "success"
    fi
    
    # Test rpm package (if available)
    if command -v rpmbuild; then
        run_test "package_rpm" "rpmbuild -ba hermit.spec" "success"
    fi
}

# Main test execution
main() {
    echo "=== Hermit CI Matrix Test ===" | tee "$TEST_LOG"
    echo "Started at: $(date)" | tee -a "$TEST_LOG"
    
    # Clean up any existing state
    rm -rf /tmp/hermitd-state 2>/dev/null || true
    mkdir -p /tmp/hermitd-state
    
    # Run test suites
    test_build_base_images
    test_runtime_features
    test_daemon_functionality
    test_security_features
    test_observability
    test_memory_safety
    test_packaging
    
    # Finalize results
    echo ']}' >> "$TEST_RESULTS"
    
    # Calculate success rate
    pass_count=$(grep -c '"status":"pass"' "$TEST_RESULTS" || echo "0")
    success_rate=$(echo "scale=2; $pass_count * 100 / $total_tests" | bc -l)
    
    echo "" | tee -a "$TEST_LOG"
    echo "=== Test Results ===" | tee -a "$TEST_LOG"
    echo "Total tests: $total_tests" | tee -a "$TEST_LOG"
    echo "Passed: $pass_count" | tee -a "$TEST_LOG"
    echo "Failed: $((total_tests - pass_count))" | tee -a "$TEST_LOG"
    echo "Success rate: $success_rate%" | tee -a "$TEST_LOG"
    echo "Completed at: $(date)" | tee -a "$TEST_LOG"
    
    # Exit with appropriate code
    if [ "$pass_count" -eq "$total_tests" ]; then
        echo "✅ All tests passed!" | tee -a "$TEST_LOG"
        exit 0
    else
        echo "❌ Some tests failed!" | tee -a "$TEST_LOG"
        exit 1
    fi
}

# Run main function
main "$@"
