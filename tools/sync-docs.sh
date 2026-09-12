#!/usr/bin/env bash
# Sync docs/user/*.md into ../noctalia-docs/src/content/docs/greeter/ as .mdx.
# Existing .mdx files keep their hand-written frontmatter (title, description,
# and sidebar metadata); only the body is refreshed from the source .md.
# New files keep their source frontmatter, falling back to a title derived from
# the first H1 when none exists. Stale .mdx files without a source document are
# removed after a successful sync. A leading H1 is dropped from the body.
# Relative links to sibling docs are rewritten to site URLs.
# Usage: tools/sync-docs.sh [docs-site-root]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
site_root="${1:-"$repo_root/../noctalia-docs"}"
dest_dir="$site_root/src/content/docs/greeter"

mkdir -p "$dest_dir"

site_route() {
    printf '%s' "$1"
}

link_exprs=()
declare -A expected_mdx=()
for md in "$repo_root"/docs/user/*.md; do
    [[ -e "$md" ]] || continue
    base="$(basename "$md" .md)"
    route="$(site_route "$base")"
    expected_mdx["$dest_dir/$route.mdx"]=1
    link_exprs+=(-e "s|]($base.md)|](/greeter/$route/)|g")
    link_exprs+=(-e "s|]($base.md#|](/greeter/$route/#|g")
done

for md in "$repo_root"/docs/user/*.md; do
    [[ -e "$md" ]] || continue
    base="$(basename "$md" .md)"
    route="$(site_route "$base")"
    mdx="$dest_dir/$route.mdx"

    source_frontmatter="$(awk 'NR == 1 && $0 == "---" { fm = 1; print; next } fm && $0 == "---" { print; exit } fm { print }' "$md")"
    source_title="$(printf '%s\n' "$source_frontmatter" | awk '/^title: / { sub(/^title: /, ""); print; exit }')"
    h1="$(awk '/^# / { sub(/^# /, ""); print; exit }' "$md")"

    frontmatter=""
    if [[ -f "$mdx" ]]; then
        frontmatter="$(awk 'NR == 1 && $0 == "---" { fm = 1; print; next } fm && $0 == "---" { print; exit } fm { print }' "$mdx")"
    fi

    title=""
    if [[ -n "$frontmatter" ]]; then
        title="$(printf '%s\n' "$frontmatter" | awk '/^title: / { sub(/^title: /, ""); print; exit }')"
    fi
    if [[ -z "$title" && -n "$source_title" ]]; then
        title="$source_title"
        frontmatter="$source_frontmatter"
    elif [[ -z "$title" ]]; then
        title="$h1"
        frontmatter="---
title: $title
---"
    fi
    if [[ -z "$title" ]]; then
        printf 'sync-docs: no title in %s, skipping\n' "$md" >&2
        continue
    fi

    {
        printf '%s\n' "$frontmatter"
        printf '\n'
        awk '
            NR == 1 && $0 == "---" { fm = 1; next }
            fm == 1 && $0 == "---" { fm = 0; next }
            fm == 1 { next }
            seen == 0 && /^# / { seen = 1; dropped = 1; next }
            dropped == 1 && /^$/ { dropped = 0; seen = 1; next }
            { seen = 1; print }
        ' "$md" | sed "${link_exprs[@]}"
    } > "$mdx.tmp"

    leftover="$(grep -Eo '\]\([^)]*\.md[^)]*\)' "$mdx.tmp" | grep -Ev '\]\(https?://' || true)"
    if [[ -n "$leftover" ]]; then
        printf 'sync-docs: warning: %s has unsynced .md links:\n' "$base.md" >&2
        sed 's/^/  /' <<<"$leftover" >&2
    fi

    mv "$mdx.tmp" "$mdx"
    printf 'synced %s -> %s\n' "$base.md" "${mdx#"$site_root"/}"
done

for mdx in "$dest_dir"/*.mdx; do
    [[ -e "$mdx" ]] || continue
    if [[ -n "${expected_mdx["$mdx"]+present}" ]]; then
        continue
    fi
    rm "$mdx"
    printf 'removed stale %s\n' "${mdx#"$site_root"/}"
done
