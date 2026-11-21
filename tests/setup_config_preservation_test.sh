#!/bin/sh
set -eu

# Root-only setup runs against mkdtemp fixtures, with no privileges over the host.
if ! command -v unshare >/dev/null 2>&1 || ! unshare --user --map-root-user true 2>/dev/null; then
    echo 'SKIP: setup preservation requires unprivileged user namespaces'
    exit 77
fi
exec unshare --user --map-root-user "$1" --setup
