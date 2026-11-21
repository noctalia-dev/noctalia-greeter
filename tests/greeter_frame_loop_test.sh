#!/bin/sh
set -eu

compositor=$1
client=$2
unset WAYLAND_DISPLAY WAYLAND_SOCKET GREETD_SOCK
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-frame-loop.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT INT TERM
mkdir "$test_dir/runtime" "$test_dir/state"
chmod 700 "$test_dir/runtime"

cat >"$test_dir/state/greeter.toml" <<'EOF'
[appearance]
hide_logo = true

[clock]
enabled = false
EOF

# Direct Power entries must never reach host power-management commands.
cat >"$test_dir/state/sync.toml" <<'EOF'
[session.power]
suspend = 'true'
reboot = 'printf "reboot\n" >> "$NOCTALIA_GREETER_TEST_POWER_LOG"'
shutdown = 'true'
EOF

# The compositor does not propagate its child's exit code, so also require the
# client's success marker. Keep the combined log available on any failure.
if ! env \
  XDG_RUNTIME_DIR="$test_dir/runtime" \
  NOCTALIA_GREETER_STATE_DIR="$test_dir/state" \
  NOCTALIA_GREETER_TEST_POWER_LOG="$test_dir/power.log" \
  WLR_BACKENDS=headless \
  WLR_HEADLESS_OUTPUTS=1 \
  WLR_RENDERER=pixman \
  LIBGL_ALWAYS_SOFTWARE=1 \
  GREETER_BIN="$client" \
  NOCTALIA_GREETER_LOG=stderr \
  "$compositor" >"$test_dir/log" 2>&1; then
  cat "$test_dir/log"
  exit 1
fi
if ! grep -Fx 'frame loop: startup, animation, redraw/layout, idle, and restart passed' "$test_dir/log" >/dev/null; then
  cat "$test_dir/log"
  exit 1
fi
