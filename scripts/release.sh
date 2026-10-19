#!/usr/bin/env bash
# Publishes a release on GitHub: the packages scripts/package.sh made in dist/ for the version in
# CMakeLists.txt, under its v<version> tag, with that version's changelog entry as the notes. A
# release already there gets the packages again (replaced).
#
#   scripts/package.sh            # on the tagged commit: the four packages, named for the version
#   scripts/release.sh [--dry-run]
#
# Needs the GitHub CLI (gh), signed in, and the tag pushed.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
dry=0
[ "${1:-}" = "--dry-run" ] && dry=1

version=$(sed -n 's/^project(kompoz VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
tag="v$version"
fail() { echo "release.sh: $*" >&2; exit 1; }

git rev-parse -q --verify "refs/tags/$tag" > /dev/null || fail "no tag $tag (git tag $tag && git push origin $tag)"
git ls-remote --exit-code --tags origin "$tag" > /dev/null || fail "tag $tag isn't on GitHub (git push origin $tag)"

files=()
for suffix in linux-x86_64.tar.gz windows-x86_64.zip android.apk web.zip; do
    file="dist/kompoz-showcase-$version-$suffix"
    [ -f "$file" ] || fail "no $file (scripts/package.sh on $tag)"
    files+=("$file")
done

# The changelog's entry for this version: from its "## <version>" heading to the next one.
# Its items' wrapped lines joined: GitHub shows a release's line breaks as they are.
notes=$(awk -v v="$version" '/^## / { on = index($0, "## " v " ") == 1 || $0 == "## " v; next } on' CHANGELOG.md |
    sed -e ':a' -e 'N' -e '$!ba' -e 's/\n  / /g' -e 's/^\n*//')
[ -n "$(echo "$notes" | tr -d '[:space:]')" ] || fail "no entry for $version in CHANGELOG.md"

if [ $dry = 1 ]; then
    echo "release.sh: would publish $tag (\"Kompoz $version\") with:"
    printf '  %s\n' "${files[@]}"
    echo "notes:"
    echo "$notes"
    exit 0
fi
if gh release view "$tag" > /dev/null 2>&1; then
    gh release upload "$tag" "${files[@]}" --clobber
    echo "release.sh: $tag's packages replaced"
else
    gh release create "$tag" "${files[@]}" --title "Kompoz $version" --notes "$notes" --verify-tag
    echo "release.sh: $tag published"
fi
