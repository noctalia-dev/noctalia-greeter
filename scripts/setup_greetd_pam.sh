#!/usr/bin/env bash
set -euo pipefail

PAM_FILE="/etc/pam.d/greetd"

find_pam_runtime_module() {
  local module
  local dir
  for module in pam_systemd.so pam_elogind.so; do
    for dir in /usr/lib/security /usr/lib64/security /lib/security /lib64/security; do
      if [[ -f "${dir}/${module}" ]]; then
        printf '%s\n' "${module}"
        return 0
      fi
    done
  done
  return 1
}

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root (use sudo)." >&2
  exit 1
fi

if [[ ! -f "${PAM_FILE}" ]]; then
  echo "info: ${PAM_FILE} not found; greetd is not installed. Skipping."
  exit 0
fi

PAM_MODULE="$(find_pam_runtime_module || true)"
if [[ -z "${PAM_MODULE}" ]]; then
  echo "warn: no pam_systemd.so or pam_elogind.so found; skipping PAM patch."
  echo "warn: use the distro-agnostic session wrapper command:"
  echo "warn:   command = \"/usr/local/bin/noctalia-greeter-session\""
  exit 0
fi

PAM_LINE="session    required     ${PAM_MODULE}"

if grep -q -E "^[[:space:]]*session[[:space:]]+.*${PAM_MODULE}([[:space:]]|$)" "${PAM_FILE}"; then
  echo "info: ${PAM_FILE} already contains ${PAM_MODULE}; nothing to do."
  exit 0
fi

backup="${PAM_FILE}.bak.noctalia.$(date +%Y%m%d%H%M%S)"
cp "${PAM_FILE}" "${backup}"
echo "info: backup created at ${backup}"

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

# Insert after last session line, or append.
if grep -q -E "^[[:space:]]*session[[:space:]]+" "${PAM_FILE}"; then
  last_session="$(grep -h -n -E "^[[:space:]]*session[[:space:]]+" "${PAM_FILE}" | tail -n 1 | awk -F: '{print $1}')"
  awk -v line="${PAM_LINE}" -v last="${last_session}" '
    {
      print $0
      if (NR == last) {
        print line
      }
    }
  ' "${PAM_FILE}" > "${tmp}"
else
  cp "${PAM_FILE}" "${tmp}"
  printf "\n%s\n" "${PAM_LINE}" >> "${tmp}"
fi

install -m 0644 "${tmp}" "${PAM_FILE}"
echo "info: patched ${PAM_FILE} with ${PAM_MODULE} session line."
