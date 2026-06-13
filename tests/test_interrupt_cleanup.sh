#!/usr/bin/env sh
set -eu

fail() {
    echo "test_interrupt_cleanup: FAIL: $1" >&2
    exit 1
}

pass() {
    echo "test_interrupt_cleanup: PASS: $1"
}

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_interrupt_cleanup: SKIP (Linux only)"
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "test_interrupt_cleanup: SKIP (requires root for current runtime setup)"
    exit 0
fi

if [ ! -x ./hermit ]; then
    fail "missing ./hermit binary; run make"
fi

if ! command -v ip >/dev/null 2>&1; then
    echo "test_interrupt_cleanup: SKIP (ip command not found)"
    exit 0
fi

before_links=$(ip -o link show | grep -c '@' || true)
before_cgroups=0
if [ -d /sys/fs/cgroup/hermit ]; then
    before_cgroups=$(ls -1 /sys/fs/cgroup/hermit 2>/dev/null | wc -l | tr -d ' ')
fi

set +e
./hermit run --net-veth --memory-max 134217728 --pids-max 64 /bin/sh -c 'sleep 20' &
runtime_pid=$!
sleep 1
kill -INT "$runtime_pid"
wait "$runtime_pid"
exit_code=$?
set -e

[ "$exit_code" -eq 130 ] || fail "expected exit code 130 on SIGINT, got $exit_code"

after_links=$(ip -o link show | grep -c '@' || true)
after_cgroups=0
if [ -d /sys/fs/cgroup/hermit ]; then
    after_cgroups=$(ls -1 /sys/fs/cgroup/hermit 2>/dev/null | wc -l | tr -d ' ')
fi

[ "$before_links" = "$after_links" ] || fail "veth leak after SIGINT (before=$before_links after=$after_links)"
[ "$before_cgroups" = "$after_cgroups" ] || fail "cgroup leak after SIGINT (before=$before_cgroups after=$after_cgroups)"

pass "SIGINT cleanup of runtime resources"
