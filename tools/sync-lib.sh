#!/usr/bin/env bash
# Mirror Noctalia's lib/, including the codec pack used by the greeter.
# Usage: tools/sync-lib.sh [path/to/noctalia]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
noctalia_root="${1:-$root/../noctalia}"
source_dir="$noctalia_root/lib"

[[ -f "$source_dir/meson.build" && -f "$noctalia_root/LICENSE" ]] || { echo "no Noctalia lib at $source_dir" >&2; exit 1; }
mirror_changes="$(git -C "$root" status --porcelain -- lib)"
if [[ -n "$mirror_changes" ]]; then
  echo "lib/ has uncommitted changes; port them to Noctalia or discard them first" >&2
  exit 1
fi

rsync -a --delete "$source_dir/" "$root/lib/"
cp "$noctalia_root/LICENSE" "$root/lib/LICENSE"
git -C "$root" status --short -- lib
