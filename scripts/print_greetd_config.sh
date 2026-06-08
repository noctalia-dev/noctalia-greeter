#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=greetd_setup_lib.sh
source "${SCRIPT_DIR}/greetd_setup_lib.sh"

SESSION_BIN="${NOCTALIA_GREETER_SESSION_BIN:-/usr/local/bin/noctalia-greeter-session}"
GREETER_USER="$(resolve_greeter_user)"

print_greetd_config_commands "${SESSION_BIN}" "${GREETER_USER}"
