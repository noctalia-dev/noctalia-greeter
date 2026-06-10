# noctalia-greeter, Justfile

# Configure debug build dir
configure:
  meson setup build --reconfigure

# Default target: build debug
build: configure
  meson compile -C build

# Configure release build dir
configure-release:
  meson setup build-release --buildtype=release --reconfigure

# Release build
build-release: configure-release
  meson compile -C build-release

# Clean all build dirs
clean:
  rm -rf build build-release

# Format code with clang-format
format:
  find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i

# Check formatting without modifying
format-check:
  find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror

# Run linters / static analysis
lint:
  cppcheck --enable=all --suppress=missingIncludeSystem src/

# Install to system
install: build
  sudo meson install -C build
  sudo ./scripts/setup_greeter_system.sh

# Ensure greetd PAM creates XDG_RUNTIME_DIR via systemd/elogind module (idempotent)
setup-greetd-pam:
  sudo ./scripts/setup_greetd_pam.sh

# Run all system setup steps needed by greetd deployments.
setup-system:
  sudo ./scripts/setup_greeter_system.sh

# Print a copy-paste greetd config block (paths/user resolved for this machine).
print-greetd-config:
  ./scripts/print_greetd_config.sh

# Create persistent log dirs for greetd (run once on the target machine)
setup-log-dir:
  sudo mkdir -p /var/lib/noctalia-greeter
  sudo touch /var/log/noctalia-greeter.log /var/lib/noctalia-greeter/greeter.log /tmp/noctalia-greeter.log
  sudo chown -R greeter:greeter /var/lib/noctalia-greeter
  sudo chown greeter:greeter /var/log/noctalia-greeter.log /tmp/noctalia-greeter.log
  sudo chmod 0664 /var/log/noctalia-greeter.log /var/lib/noctalia-greeter/greeter.log /tmp/noctalia-greeter.log
  @echo "Logs: /var/log/noctalia-greeter.log and /var/lib/noctalia-greeter/greeter.log"

# Verify greeter user can write logs (run on target machine after setup-log-dir)
log-test: build
  sudo -u greeter env GREETD_SOCK=1 XDG_VTNR=7 ./build/noctalia-greeter --log-test
  @echo "--- /var/log/noctalia-greeter.log ---"
  @cat /var/log/noctalia-greeter.log | tail -5

# Uninstall
uninstall:
  sudo ninja uninstall -C build

# Run under the built-in compositor (same as greetd should use)
run: build
  dbus-run-session -- ./build/noctalia-greeter-compositor ./build/noctalia-greeter

# Run under the compositor on your login session. Logs to ~/.cache/noctalia-greeter.log
run-local: build
  #!/usr/bin/env bash
  set -euo pipefail
  log="${NOCTALIA_GREETER_LOG:-$HOME/.cache/noctalia-greeter.log}"
  mkdir -p "$(dirname "$log")"
  echo "user=$USER log=$log"
  echo "Recovery: just recover"
  env NOCTALIA_GREETER_LOG="$log" dbus-run-session -- \
    ./build/noctalia-greeter-compositor ./build/noctalia-greeter

# Configure AddressSanitizer build dir
configure-asan:
  meson setup build-asan --buildtype=debug -Db_sanitize=address -Db_lundef=false --reconfigure

# Build AddressSanitizer variant
build-asan: configure-asan
  meson compile -C build-asan

# Run under the compositor with AddressSanitizer enabled
run-asan: build-asan
  ASAN_OPTIONS=abort_on_error=1:fast_unwind_on_malloc=0:symbolize=1 \
  dbus-run-session -- ./build-asan/noctalia-greeter-compositor ./build-asan/noctalia-greeter

# Run in your current Wayland session (niri, sway, etc.), no nested compositor
run-niri: build
  #!/usr/bin/env bash
  set -euo pipefail
  if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "WAYLAND_DISPLAY is not set; run from a terminal inside niri."
    exit 1
  fi
  log="${NOCTALIA_GREETER_LOG:-$HOME/.cache/noctalia-greeter.log}"
  mkdir -p "$(dirname "$log")"
  echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY log=$log"
  echo "Auth needs greetd; unset GREETD_SOCK for UI-only dev, or point at a test socket."
  env -u GREETD_SOCK NOCTALIA_GREETER_LOG="$log" ./build/noctalia-greeter

# Kill greeter and stop greetd when the display is stuck (run over SSH or blind)
recover:
  #!/usr/bin/env bash
  sudo killall noctalia-greeter 2>/dev/null || true
  sudo killall noctalia-greeter-compositor 2>/dev/null || true
  sudo sv stop greetd 2>/dev/null || true
  echo "If the screen is still wrong: sudo chvt 2"

# Run as greeter user. Needs a greetd session on that VT; use run-local if manual login fails.
run-greeter bin="/usr/local/bin/noctalia-greeter":
  @echo "Ensure greetd is stopped: sudo sv stop greetd"
  sudo -u greeter dbus-run-session -- \
    /usr/local/bin/noctalia-greeter-compositor {{bin}}

run-greeter-dev: build
  @echo "Ensure greetd is stopped: sudo sv stop greetd"
  sudo -u greeter dbus-run-session -- \
    ./build/noctalia-greeter-compositor ./build/noctalia-greeter
