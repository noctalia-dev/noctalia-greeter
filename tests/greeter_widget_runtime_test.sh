#!/bin/sh
set -eu
compositor=$1
client=$2
unset WAYLAND_DISPLAY WAYLAND_SOCKET GREETD_SOCK
widget_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-widget-runtime.XXXXXX")
trap 'rm -rf "$widget_test_dir"' EXIT INT TERM
mkdir -p "$widget_test_dir/runtime" "$widget_test_dir/state" "$widget_test_dir/data/wayland-sessions"
chmod 700 "$widget_test_dir/runtime"
cat >"$widget_test_dir/data/wayland-sessions/widget-test.desktop" <<'EOF'
[Desktop Entry]
Name=Widget Runtime Test
Exec=/bin/true
Type=Application
EOF
cat >"$widget_test_dir/state/sync.toml" <<'EOF'
[session.power]
suspend='true'
reboot='true'
shutdown='true'
EOF
if ! env \
  XDG_RUNTIME_DIR="$widget_test_dir/runtime" \
  XDG_DATA_DIRS="$widget_test_dir/data" \
  NOCTALIA_GREETER_STATE_DIR="$widget_test_dir/state" \
  WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman \
  LIBGL_ALWAYS_SOFTWARE=1 GREETER_BIN="$client" NOCTALIA_GREETER_LOG=stderr \
  "$compositor" >"$widget_test_dir/log" 2>&1; then
  cat "$widget_test_dir/log"
  exit 1
fi
if ! grep -Fx 'widget runtime: mounted profiles and shared demand-driven sampling passed' "$widget_test_dir/log" >/dev/null; then
  cat "$widget_test_dir/log"
  exit 1
fi
