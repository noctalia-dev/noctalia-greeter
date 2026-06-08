# Shared helpers for greetd setup scripts. Source this file; do not execute.

_greetd_setup_lib_dir() {
  cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

find_apply_appearance() {
  local script_dir
  script_dir="$(_greetd_setup_lib_dir)"

  local candidate
  for candidate in \
    "${NOCTALIA_GREETER_APPLY_APPEARANCE:-}" \
    /usr/local/bin/noctalia-greeter-apply-appearance \
    /usr/bin/noctalia-greeter-apply-appearance \
    "${script_dir}/../build/noctalia-greeter-apply-appearance" \
    "${script_dir}/../build-user/noctalia-greeter-apply-appearance"; do
    if [[ -n "${candidate}" && -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  return 1
}

resolve_greeter_user() {
  if [[ -n "${GREETER_USER:-}" ]]; then
    echo "${GREETER_USER}"
    return 0
  fi

  local helper=""
  if helper="$(find_apply_appearance)"; then
    local user=""
    if user="$("${helper}" --print-greeter-user 2>/dev/null)"; then
      echo "${user}"
      return 0
    fi
  fi

  echo "warn: could not resolve greeter user from greetd config; defaulting to 'greeter'" >&2
  echo "greeter"
}

print_greetd_config_commands() {
  local session_bin="$1"
  local greeter_user="$2"

  echo "Configure greetd — copy and paste the block below."
  echo "On an existing greetd setup this replaces config.toml (a .bak copy is made first)."
  echo ""
  cat <<EOF
sudo useradd -r -s /usr/bin/nologin -d /var/lib/noctalia-greeter ${greeter_user} 2>/dev/null || true

sudo cp -a /etc/greetd/config.toml /etc/greetd/config.toml.bak 2>/dev/null || true
sudo tee /etc/greetd/config.toml >/dev/null <<'GREETD_CONFIG'
[terminal]
vt = 1

[default_session]
command = "${session_bin}"
user = "${greeter_user}"
GREETD_CONFIG

sudo systemctl enable --now greetd
EOF
  echo ""
  echo "On runit: sudo sv restart greetd"
  echo ""
  echo "Optional pinned desktop session (name must match the picker):"
  echo "  command = \"${session_bin} -- --session niri\""
}
