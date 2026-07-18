# Shared helpers for greetd setup scripts. Source this file; do not execute.

_greetd_setup_lib_dir() {
  cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

find_session_bin() {
  local script_dir
  script_dir="$(_greetd_setup_lib_dir)"

  local candidate
  for candidate in \
    "${NOCTALIA_GREETER_SESSION_BIN:-}" \
    /usr/bin/noctalia-greeter-session \
    /usr/local/bin/noctalia-greeter-session \
    "${script_dir}/noctalia-greeter-session" \
    "${script_dir}/../build/noctalia-greeter-session"; do
    if [[ -n "${candidate}" && -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  echo "/usr/bin/noctalia-greeter-session"
}

find_apply_appearance() {
  local script_dir
  script_dir="$(_greetd_setup_lib_dir)"

  local candidate
  for candidate in \
    "${NOCTALIA_GREETER_APPLY_APPEARANCE:-}" \
    /usr/bin/noctalia-greeter-apply-appearance \
    /usr/local/bin/noctalia-greeter-apply-appearance \
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

# Portable state-dir setup (OpenRC, runit, systemd, …). Prefer this over
# tmpfiles alone — it uses the real greetd session user, not a hardcoded name.
# Logging defaults to stderr; do not create a log file here.
ensure_greeter_paths() {
  local greeter_user="${1:-greeter}"
  local state_dir="${NOCTALIA_GREETER_STATE_DIR:-/var/lib/noctalia-greeter}"

  mkdir -p "${state_dir}"
  chmod 0750 "${state_dir}"

  if id -u "${greeter_user}" >/dev/null 2>&1; then
    chown -R "${greeter_user}:${greeter_user}" "${state_dir}"
  else
    echo "warn: user '${greeter_user}' does not exist yet; skipping path chown." >&2
  fi
}

greetd_service_command() {
  if command -v systemctl >/dev/null 2>&1; then
    echo "sudo systemctl enable --now greetd"
    return 0
  fi
  if command -v sv >/dev/null 2>&1; then
    echo "sudo sv restart greetd"
    return 0
  fi
  echo "# enable and restart greetd using your init system"
}

print_greetd_config_commands() {
  local session_bin="$1"
  local greeter_user="$2"
  local service_cmd
  service_cmd="$(greetd_service_command)"

  echo "Configure greetd - copy and paste the block below."
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

${service_cmd}
EOF
  echo ""
  echo "Optional pinned desktop session (name must match the picker):"
  echo "  command = \"${session_bin} -- --session niri\""
}
