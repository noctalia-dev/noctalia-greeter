#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root (use sudo)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=greetd_setup_lib.sh
source "${SCRIPT_DIR}/greetd_setup_lib.sh"

SESSION_BIN="${NOCTALIA_GREETER_SESSION_BIN:-/usr/local/bin/noctalia-greeter-session}"
SYNCED_DATA_DIR="/var/lib/noctalia-greeter"

APPLY_APPEARANCE="$(find_apply_appearance || true)"
GREETER_USER="$(resolve_greeter_user)"

echo "info: applying greetd PAM runtime module patch..."
"${SCRIPT_DIR}/setup_greetd_pam.sh"

echo "info: preparing greeter paths..."
mkdir -p "${SYNCED_DATA_DIR}"
chmod 0755 "${SYNCED_DATA_DIR}"
if id -u "${GREETER_USER}" >/dev/null 2>&1; then
  chown "${GREETER_USER}:${GREETER_USER}" "${SYNCED_DATA_DIR}"
fi
touch /var/log/noctalia-greeter.log /var/lib/noctalia-greeter/greeter.log /tmp/noctalia-greeter.log

if id -u "${GREETER_USER}" >/dev/null 2>&1; then
  chown "${GREETER_USER}:${GREETER_USER}" /var/log/noctalia-greeter.log \
    /var/lib/noctalia-greeter/greeter.log /tmp/noctalia-greeter.log
else
  echo "warn: user '${GREETER_USER}' does not exist yet; skipping log chown."
fi

chmod 0664 /var/log/noctalia-greeter.log /var/lib/noctalia-greeter/greeter.log /tmp/noctalia-greeter.log

if [[ -n "${APPLY_APPEARANCE}" ]]; then
  echo "info: installing greeter.conf via ${APPLY_APPEARANCE} --setup-system"
  GREETER_USER="${GREETER_USER}" "${APPLY_APPEARANCE}" --setup-system
else
  echo "error: noctalia-greeter-apply-appearance not found; build/install first." >&2
  exit 1
fi

echo "info: appearance sync target: ${SYNCED_DATA_DIR}"

if [[ ! -x "${SESSION_BIN}" ]]; then
  echo "warn: session launcher '${SESSION_BIN}' not found or not executable."
  echo "warn: install first via: sudo meson install -C build"
fi

echo
echo "System setup complete."
echo
print_greetd_config_commands "${SESSION_BIN}" "${GREETER_USER}"
