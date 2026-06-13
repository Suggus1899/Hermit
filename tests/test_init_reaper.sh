#!/usr/bin/env sh
set -eu

fail() {
    echo "test_init_reaper: FAIL: $1" >&2
    exit 1
}

pass() {
    echo "test_init_reaper: PASS: $1"
}

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_init_reaper: SKIP (Linux only)"
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "test_init_reaper: SKIP (requires root for current runtime setup)"
    exit 0
fi

if [ ! -x ./hermit ]; then
    fail "missing ./hermit binary; run make"
fi

# Case 1: signal forwarding from init PID1 to payload process group.
set +e
./hermit run /bin/sh -c 'trap "exit 99" TERM; while true; do sleep 1; done' &
hermit_pid=$!
set -e

child_pid=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ -r "/proc/$hermit_pid/task/$hermit_pid/children" ]; then
        child_pid=$(awk '{print $1}' "/proc/$hermit_pid/task/$hermit_pid/children")
        if [ -n "$child_pid" ]; then
            break
        fi
    fi
    sleep 0.1
done

[ -n "$child_pid" ] || fail "could not determine container init pid from /proc"

kill -TERM "$child_pid"

set +e
wait "$hermit_pid"
exit_code=$?
set -e

[ "$exit_code" -eq 99 ] || fail "expected hermit exit code 99 after forwarded TERM, got $exit_code"
pass "signal forwarding init->payload"

# Case 2: reaper stress (many short-lived children across repeated runs).
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    ./hermit run /bin/sh -c 'n=0; while [ "$n" -lt 20 ]; do (sleep 0.01 &) ; n=$((n+1)); done; exit 0' ||
        fail "reaper stress failed at iteration $i"
done

pass "reaper stress runs completed"
echo "test_init_reaper: OK"
