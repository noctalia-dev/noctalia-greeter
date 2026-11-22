#!/usr/bin/env bash
# Mirror Noctalia's desktop-widgets/ into this repository.
# Usage: tools/sync-desktopwidgets.sh [path/to/noctalia]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
noctalia_root="${1:-$root/../noctalia}"
source_dir="$noctalia_root/desktop-widgets"

[[ -f "$source_dir/meson.build" && -f "$noctalia_root/LICENSE" ]] || { echo "no Noctalia widgets at $source_dir" >&2; exit 1; }
mirror_changes="$(git -C "$root" status --porcelain -- desktop-widgets)"
if [[ -n "$mirror_changes" ]]; then
  echo "desktop-widgets/ has uncommitted changes; port them to Noctalia or discard them first" >&2
  exit 1
fi

rsync -a --delete "$source_dir/" "$root/desktop-widgets/"
cp "$noctalia_root/LICENSE" "$root/desktop-widgets/LICENSE"
git -C "$root" status --short -- desktop-widgets
