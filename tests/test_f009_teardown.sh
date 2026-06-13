#!/bin/bash
# Test F009: Teardown transaccional runtime (100 ciclos run/kill sin fugas)

set -e

HERMIT_BIN="./hermit"
TEST_LOG="/tmp/hermit_f009_test.log"
CYCLES=100
FAILED_CYCLES=0

echo "=== F009 Test: Teardown transaccional runtime ===" | tee "$TEST_LOG"
echo "Testing $CYCLES cycles of run/kill without resource leaks" | tee -a "$TEST_LOG"
echo "Started at: $(date)" | tee -a "$TEST_LOG"

# Cleanup function
cleanup_test() {
    echo "Cleaning up test environment..." | tee -a "$TEST_LOG"
    
    # Remove any remaining hermit cgroups
    if [ -d "/sys/fs/cgroup/hermit" ]; then
        find /sys/fs/cgroup/hermit -name "*" -type d -exec rmdir {} \; 2>/dev/null || true
        rmdir /sys/fs/cgroup/hermit 2>/dev/null || true
    fi
    
    # Remove any remaining veth interfaces
    ip link show | grep -E "h[0-9]+h" | awk '{print $2}' | cut -d@ -f1 | while read iface; do
        if [ -n "$iface" ]; then
            ip link del "$iface" 2>/dev/null || true
        fi
    done
    
    # Remove any remaining NAT rules
    iptables -t nat -L POSTROUTING -n | grep "10.20.0" | while read line; do
        rule=$(echo "$line" | awk '{print $1, $2, $3, $4, $5, $6, $7, $8, $9}')
        if [ -n "$rule" ]; then
            iptables -t nat -D POSTROUTING $rule 2>/dev/null || true
        fi
    done
}

# Initial cleanup
cleanup_test

# Count initial resources
initial_cgroups=$(find /sys/fs/cgroup/hermit -type d 2>/dev/null | wc -l)
initial_veths=$(ip link show | grep -cE "h[0-9]+h" || echo 0)
initial_nat_rules=$(iptables -t nat -L POSTROUTING -n | grep -c "10.20.0" || echo 0)

echo "Initial state:" | tee -a "$TEST_LOG"
echo "  Cgroups: $initial_cgroups" | tee -a "$TEST_LOG"
echo "  Veth interfaces: $initial_veths" | tee -a "$TEST_LOG"
echo "  NAT rules: $initial_nat_rules" | tee -a "$TEST_LOG"

# Test cycles
for i in $(seq 1 $CYCLES); do
    echo -n "Cycle $i/$CYCLES... " | tee -a "$TEST_LOG"
    
    # Start container in background with network
    $HERMIT_BIN run --net-veth --net-nat --out-if lo --cpu-max "50000 100000" --memory-max "134217728" sleep 30 &
    CONTAINER_PID=$!
    
    # Wait a bit for setup
    sleep 0.5
    
    # Kill the container
    kill $CONTAINER_PID 2>/dev/null || true
    wait $CONTAINER_PID 2>/dev/null || true
    
    # Check for immediate resource leaks
    current_cgroups=$(find /sys/fs/cgroup/hermit -type d 2>/dev/null | wc -l)
    current_veths=$(ip link show | grep -cE "h[0-9]+h" || echo 0)
    current_nat_rules=$(iptables -t nat -L POSTROUTING -n | grep -c "10.20.0" || echo 0)
    
    # Allow some time for cleanup (with retries)
    cleanup_wait=0
    max_wait=5
    while [ $cleanup_wait -lt $max_wait ]; do
        if [ $current_cgroups -eq $initial_cgroups ] && [ $current_veths -eq $initial_veths ] && [ $current_nat_rules -eq $initial_nat_rules ]; then
            break
        fi
        sleep 1
        cleanup_wait=$((cleanup_wait + 1))
        current_cgroups=$(find /sys/fs/cgroup/hermit -type d 2>/dev/null | wc -l)
        current_veths=$(ip link show | grep -cE "h[0-9]+h" || echo 0)
        current_nat_rules=$(iptables -t nat -L POSTROUTING -n | grep -c "10.20.0" || echo 0)
    done
    
    # Check for leaks
    if [ $current_cgroups -gt $initial_cgroups ] || [ $current_veths -gt $initial_veths ] || [ $current_nat_rules -gt $initial_nat_rules ]; then
        echo "FAILED - Resource leak detected!" | tee -a "$TEST_LOG"
        echo "  Cgroups: $current_cgroups (expected $initial_cgroups)" | tee -a "$TEST_LOG"
        echo "  Veths: $current_veths (expected $initial_veths)" | tee -a "$TEST_LOG"
        echo "  NAT rules: $current_nat_rules (expected $initial_nat_rules)" | tee -a "$TEST_LOG"
        FAILED_CYCLES=$((FAILED_CYCLES + 1))
    else
        echo "OK" | tee -a "$TEST_LOG"
    fi
    
    # Progress indicator every 10 cycles
    if [ $((i % 10)) -eq 0 ]; then
        echo "Progress: $i/$CYCLES cycles completed, $FAILED_CYCLES failed" | tee -a "$TEST_LOG"
    fi
done

# Final resource count
final_cgroups=$(find /sys/fs/cgroup/hermit -type d 2>/dev/null | wc -l)
final_veths=$(ip link show | grep -cE "h[0-9]+h" || echo 0)
final_nat_rules=$(iptables -t nat -L POSTROUTING -n | grep -c "10.20.0" || echo 0)

echo "" | tee -a "$TEST_LOG"
echo "=== Final Results ===" | tee -a "$TEST_LOG"
echo "Total cycles: $CYCLES" | tee -a "$TEST_LOG"
echo "Failed cycles: $FAILED_CYCLES" | tee -a "$TEST_LOG"
echo "Success rate: $(echo "scale=2; ($CYCLES - $FAILED_CYCLES) * 100 / $CYCLES" | bc -l)%" | tee -a "$TEST_LOG"
echo "" | tee -a "$TEST_LOG"
echo "Final resource count:" | tee -a "$TEST_LOG"
echo "  Cgroups: $final_cgroups (started with $initial_cgroups)" | tee -a "$TEST_LOG"
echo "  Veth interfaces: $final_veths (started with $initial_veths)" | tee -a "$TEST_LOG"
echo "  NAT rules: $final_nat_rules (started with $initial_nat_rules)" | tee -a "$TEST_LOG"
echo "Test completed at: $(date)" | tee -a "$TEST_LOG"

# Final cleanup
cleanup_test

# Exit with appropriate code
if [ $FAILED_CYCLES -eq 0 ] && [ $final_cgroups -eq $initial_cgroups ] && [ $final_veths -eq $initial_veths ] && [ $final_nat_rules -eq $initial_nat_rules ]; then
    echo "" | tee -a "$TEST_LOG"
    echo "✅ F009 TEST PASSED: No resource leaks detected after $CYCLES cycles" | tee -a "$TEST_LOG"
    exit 0
else
    echo "" | tee -a "$TEST_LOG"
    echo "❌ F009 TEST FAILED: Resource leaks detected" | tee -a "$TEST_LOG"
    exit 1
fi
