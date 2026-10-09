#!/bin/sh
set -eu

compositor=$1
true_program=$2
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-compositor-runtime.XXXXXX")
state_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-compositor-state.XXXXXX")
log_file="$state_dir/compositor.log"

cleanup() {
  rm -rf "$runtime_dir" "$state_dir"
}
trap cleanup EXIT INT TERM

cat >"$state_dir/greeter.toml" <<'EOF'
[output]
layout = "HEADLESS-1:0,0"
scales = "HEADLESS-1:1"
EOF

run_compositor() {
  env \
    XDG_RUNTIME_DIR="$runtime_dir" \
    NOCTALIA_GREETER_STATE_DIR="$state_dir" \
    WLR_BACKENDS=headless \
    WLR_HEADLESS_OUTPUTS=2 \
    WLR_RENDERER=pixman \
    GREETER_BIN="$true_program" \
    NOCTALIA_GREETER_LOG=stderr \
    "$compositor" >"$log_file" 2>&1
}

run_compositor

grep -F "greeter output: HEADLESS-1 at (0,0)" "$log_file" >/dev/null
grep -F "output_layout: 'HEADLESS-2' not listed; placed at (1280,0)" "$log_file" >/dev/null

cat >"$state_dir/greeter.toml" <<'EOF'
[output]
EOF
cat >"$state_dir/sync.toml" <<'EOF'
[output]
layout = "HEADLESS-1:0,0; HEADLESS-2:640,0"
transforms = "HEADLESS-1:180; HEADLESS-2:normal"
scales = "HEADLESS-1:2; HEADLESS-2:1"
EOF

run_compositor
grep -F "output HEADLESS-1 scale=2.00 transform=2" "$log_file" >/dev/null
grep -F "greeter output: HEADLESS-2 at (640,0)" "$log_file" >/dev/null

cat >"$state_dir/greeter.toml" <<'EOF'
[output]
use_synced_settings = false
EOF

run_compositor
grep -F "output HEADLESS-1 scale=1.00 transform=0" "$log_file" >/dev/null
grep -F "output HEADLESS-2 scale=1.00 transform=0" "$log_file" >/dev/null
grep -F "greeter output: HEADLESS-2 at (1280,0)" "$log_file" >/dev/null
