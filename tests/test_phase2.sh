#!/usr/bin/env sh
set -eu

fail() {
    echo "test_phase2: FAIL: $1" >&2
    exit 1
}

pass() {
    echo "test_phase2: PASS: $1"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

require_cmd uname
require_cmd id
require_cmd hostname
require_cmd readlink
require_cmd sh

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_phase2: SKIP (requires Linux)"
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "test_phase2: SKIP (requires root for CLONE_NEWNS/CLONE_NEWNET in current setup)"
    exit 0
fi

if [ ! -x ./hermit ]; then
    fail "hermit binary not found or not executable; run make hermit first"
fi

pid_inside=$(./hermit run /bin/sh -c 'echo $$' | tr -d '\r\n')
[ "$pid_inside" = "1" ] || fail "expected PID 1 inside container, got '$pid_inside'"
pass "PID namespace gives PID 1"

host_before=$(hostname)
container_host=$(./hermit run --hostname hermit-phase2 /bin/hostname | tr -d '\r\n')
host_after=$(hostname)
[ "$container_host" = "hermit-phase2" ] || fail "container hostname mismatch: '$container_host'"
[ "$host_before" = "$host_after" ] || fail "host hostname changed unexpectedly"
pass "UTS namespace isolates hostname"

host_netns=$(readlink /proc/self/ns/net)
child_netns=$(./hermit run /bin/sh -c 'readlink /proc/self/ns/net' | tr -d '\r\n')
[ "$host_netns" != "$child_netns" ] || fail "network namespace not isolated"
pass "Network namespace is isolated"

host_mntns=$(readlink /proc/self/ns/mnt)
child_mntns=$(./hermit run /bin/sh -c 'readlink /proc/self/ns/mnt' | tr -d '\r\n')
[ "$host_mntns" != "$child_mntns" ] || fail "mount namespace not isolated"
pass "Mount namespace is isolated"

echo "test_phase2: OK"
