#!/usr/bin/env sh
set -eu

fail() {
    echo "test_all_phases: FAIL: $1" >&2
    exit 1
}

pass() {
    echo "test_all_phases: PASS: $1"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

require_cmd sh
require_cmd readlink
require_cmd hostname

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_all_phases: SKIP (Linux only)"
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "test_all_phases: SKIP (requires root privileges for current implementation)"
    exit 0
fi

if [ ! -x ./hermit ]; then
    fail "missing ./hermit binary; run make"
fi

# Phase 2 checks
pid_inside=$(./hermit run /bin/sh -c 'echo $$' | tr -d '\r\n')
[ "$pid_inside" = "1" ] || fail "phase2: expected PID 1, got '$pid_inside'"

host_before=$(hostname)
container_host=$(./hermit run --hostname hermit-full /bin/hostname | tr -d '\r\n')
host_after=$(hostname)
[ "$container_host" = "hermit-full" ] || fail "phase2: hostname mismatch"
[ "$host_before" = "$host_after" ] || fail "phase2: host hostname changed"

host_netns=$(readlink /proc/self/ns/net)
child_netns=$(./hermit run /bin/sh -c 'readlink /proc/self/ns/net' | tr -d '\r\n')
[ "$host_netns" != "$child_netns" ] || fail "phase2: net namespace not isolated"

host_mntns=$(readlink /proc/self/ns/mnt)
child_mntns=$(./hermit run /bin/sh -c 'readlink /proc/self/ns/mnt' | tr -d '\r\n')
[ "$host_mntns" != "$child_mntns" ] || fail "phase2: mount namespace not isolated"
pass "phase2 namespaces"

# Phase 3 checks (pivot_root + /proc)
if command -v mount >/dev/null 2>&1; then
    ROOTFS_DIR="$(mktemp -d /tmp/hermit-rootfs.XXXXXX)"
    cleanup_rootfs() {
        mountpoint -q "$ROOTFS_DIR" 2>/dev/null && umount "$ROOTFS_DIR" || true
        rm -rf "$ROOTFS_DIR"
    }
    trap cleanup_rootfs EXIT INT TERM

    mount --bind / "$ROOTFS_DIR"
    ./hermit run --rootfs "$ROOTFS_DIR" /bin/sh -c 'test -r /proc/1/stat'
    pass "phase3 rootfs/proc"

    cleanup_rootfs
    trap - EXIT INT TERM
else
    echo "test_all_phases: SKIP phase3 (mount command not found)"
fi

# Phase 4 checks (cgroup v2)
if [ -d /sys/fs/cgroup ]; then
    ./hermit run --memory-max 134217728 --pids-max 64 /bin/true
    if [ ! -d /sys/fs/cgroup/hermit ]; then
        fail "phase4: cgroup root not created"
    fi

    newest_group=$(ls -1t /sys/fs/cgroup/hermit 2>/dev/null | head -n 1 || true)
    [ -n "$newest_group" ] || fail "phase4: no child cgroup created"

    memv=$(cat "/sys/fs/cgroup/hermit/$newest_group/memory.max" 2>/dev/null || true)
    pidsv=$(cat "/sys/fs/cgroup/hermit/$newest_group/pids.max" 2>/dev/null || true)
    [ "$memv" = "134217728" ] || fail "phase4: memory.max mismatch ('$memv')"
    [ "$pidsv" = "64" ] || fail "phase4: pids.max mismatch ('$pidsv')"
    pass "phase4 cgroups"
else
    echo "test_all_phases: SKIP phase4 (/sys/fs/cgroup missing)"
fi

# Phase 5 checks (veth)
if command -v ip >/dev/null 2>&1; then
    before_count=$(ip -o link show | grep -c '@' || true)
    ./hermit run --net-veth /bin/sh -c 'ip link show lo >/dev/null'
    after_count=$(ip -o link show | grep -c '@' || true)
    [ "$before_count" = "$after_count" ] || fail "phase5: veth leak detected on host"
    pass "phase5 veth setup"
else
    echo "test_all_phases: SKIP phase5 (ip command not found)"
fi

echo "test_all_phases: OK"
