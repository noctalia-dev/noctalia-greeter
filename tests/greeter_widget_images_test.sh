#!/bin/sh
set -eu
compositor=$1
client=$2
unset WAYLAND_DISPLAY WAYLAND_SOCKET GREETD_SOCK
image_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-widget-images-run.XXXXXX")
trap 'rm -rf "$image_test_dir"' EXIT INT TERM
mkdir "$image_test_dir/runtime" "$image_test_dir/state"
chmod 700 "$image_test_dir/runtime" "$image_test_dir/state"
if ! env XDG_RUNTIME_DIR="$image_test_dir/runtime" \
  NOCTALIA_GREETER_STATE_DIR="$image_test_dir/state" \
  WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman \
  LIBGL_ALWAYS_SOFTWARE=1 GREETER_BIN="$client" NOCTALIA_GREETER_LOG=stderr \
  "$compositor" >"$image_test_dir/log" 2>&1; then
  cat "$image_test_dir/log"
  exit 1
fi
if ! grep -Fx 'widget images: installed assets, sharing, recovery and teardown passed' "$image_test_dir/log" >/dev/null; then
  cat "$image_test_dir/log"
  exit 1
fi
