#!/usr/bin/env sh
set -eu

fail() {
    echo "test_userns: FAIL: $1" >&2
    exit 1
}

pass() {
    echo "test_userns: PASS: $1"
}

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_userns: SKIP (Linux only)"
    exit 0
fi

if [ ! -x ./hermit ]; then
    fail "missing ./hermit binary; run make"
fi

# Validate user namespace mapping: uid/gid inside should be 0 when --userns is enabled.
uid_inside=$(./hermit run --userns /bin/sh -c 'id -u' | tr -d '\r\n' || true)
gid_inside=$(./hermit run --userns /bin/sh -c 'id -g' | tr -d '\r\n' || true)

[ "$uid_inside" = "0" ] || fail "expected uid 0 inside userns, got '$uid_inside'"
[ "$gid_inside" = "0" ] || fail "expected gid 0 inside userns, got '$gid_inside'"

pass "user namespace uid/gid mapping"
echo "test_userns: OK"
