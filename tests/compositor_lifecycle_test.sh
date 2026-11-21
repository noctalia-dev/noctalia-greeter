#!/bin/sh
set -eu

compositor=$1
client=$2
unset WAYLAND_DISPLAY WAYLAND_SOCKET GREETD_SOCK
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-lifecycle-run.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT INT TERM
mkdir "$test_dir/runtime" "$test_dir/state"
chmod 700 "$test_dir/runtime"

for stage in hotplug-idle hotplug-owner; do
  if ! env \
    XDG_RUNTIME_DIR="$test_dir/runtime" \
    NOCTALIA_GREETER_STATE_DIR="$test_dir/state" \
    WLR_BACKENDS=headless \
    WLR_HEADLESS_OUTPUTS=2 \
    WLR_RENDERER=pixman \
    LIBGL_ALWAYS_SOFTWARE=1 \
    GREETER_BIN="$client" \
    NOCTALIA_GREETER_LOG=stderr \
    "$compositor" -- "$stage" >"$test_dir/log" 2>&1; then
    cat "$test_dir/log"
    exit 1
  fi
  if ! grep -Fx 'user switch: cancellation and account isolation passed' "$test_dir/log" >/dev/null ||
     ! grep -Fx 'compositor lifecycle: output destruction, reassignment and client admission passed' "$test_dir/log" >/dev/null; then
    cat "$test_dir/log"
    exit 1
  fi
done
