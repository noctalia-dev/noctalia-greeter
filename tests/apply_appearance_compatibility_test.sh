#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: %s APPLY_HELPER\n' "$0" >&2
    exit 2
fi

helper=$1
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-greeter-compat.XXXXXX")
cleanup() {
    rm -f "$test_dir/expected" "$test_dir/stdout" "$test_dir/stderr"
    rmdir "$test_dir"
}
trap cleanup EXIT HUP INT TERM

printf 'secure-sync-v1\n' > "$test_dir/expected"
if ! "$helper" --supports secure-sync-v1 > "$test_dir/stdout" 2> "$test_dir/stderr"; then
    printf 'secure-sync-v1 capability probe failed\n' >&2
    exit 1
fi
if ! cmp -s "$test_dir/expected" "$test_dir/stdout" || [ -s "$test_dir/stderr" ]; then
    printf 'secure-sync-v1 capability response was not exact\n' >&2
    exit 1
fi

status=0
"$helper" --supports secure-sync-v2 > "$test_dir/stdout" 2> "$test_dir/stderr" || status=$?
if [ "$status" -ne 2 ] || [ -s "$test_dir/stdout" ] || ! grep -Fq -- '--supports secure-sync-v1' "$test_dir/stderr"; then
    printf 'unsupported capability was not rejected with usage\n' >&2
    exit 1
fi

# Noctalia 5.0.1 and older invoke the helper with one positional staging path.
# A missing path must reach the retained legacy handler (exit 1), not argument
# parsing (exit 2). It intentionally cannot install anything during this test.
status=0
"$helper" "$test_dir/missing-staging" > "$test_dir/stdout" 2> "$test_dir/stderr" || status=$?
if [ "$status" -ne 1 ] || grep -Fq 'usage:' "$test_dir/stdout" "$test_dir/stderr"; then
    printf 'legacy positional helper interface is no longer accepted\n' >&2
    exit 1
fi
