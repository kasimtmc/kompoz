#!/usr/bin/env bash
# Downloads the sources Kompoz builds itself on platforms without system packages
# (Android, iOS, Windows without vcpkg): FreeType, HarfBuzz, zlib, libpng and the Vulkan
# headers, at pinned versions, and applies the fixes in scripts/patches/<name>-*.patch.
# Usage: scripts/fetch-deps.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$root/third_party"
cache="$dest/.downloads"
mkdir -p "$cache"

fetch() {  # name version url sha256
    local name=$1 version=$2 url=$3 sha=$4
    local file="$cache/$(basename "$url")"
    local patches=()
    for p in "$root/scripts/patches/$name"-*.patch; do [[ -e "$p" ]] && patches+=("$p"); done
    # The version and the patches on it: a change to either extracts it again.
    local stamp="$version"
    if (( ${#patches[@]} )); then stamp="$version+$(cat "${patches[@]}" | sha256sum | cut -c1-12)"; fi
    if [[ -f "$dest/$name/.version" && "$(cat "$dest/$name/.version")" == "$stamp" ]]; then
        echo "$name $stamp: already present"
        return
    fi
    if [[ ! -f "$file" ]]; then
        echo "$name $version: downloading $url"
        curl -fL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --quiet - || { echo "$name: checksum mismatch" >&2; rm -f "$file"; exit 1; }
    rm -rf "${dest:?}/$name"
    mkdir -p "$dest/$name"
    # -m: fresh file times, so incremental builds (and precompiled headers) see the new sources.
    tar -xmf "$file" -C "$dest/$name" --strip-components=1
    for p in "${patches[@]}"; do
        patch -p1 -s -d "$dest/$name" < "$p" || { echo "$name: $(basename "$p") doesn't apply" >&2; exit 1; }
        echo "$name: applied $(basename "$p")"
    done
    echo "$stamp" > "$dest/$name/.version"
    echo "$name $stamp: ready"
}

fetch freetype 2.14.3 \
    https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
    36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f

fetch harfbuzz 14.5.0 \
    https://github.com/harfbuzz/harfbuzz/releases/download/14.5.0/harfbuzz-14.5.0.tar.xz \
    b7132e148358a45185c9feafd049dbaf243649d3c44414b3534d9c95d18592b9

# PNG: images read and screenshots written (libpng, over zlib's deflate).
fetch zlib 1.3.2 \
    https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.xz \
    d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3

fetch libpng 1.6.59 \
    https://download.sourceforge.net/libpng/libpng-1.6.59.tar.xz \
    d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389

# The GPU renderer's Vulkan backend: the API's headers, for where the system has none.
fetch Vulkan-Headers 1.4.363.0 \
    https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz \
    4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e
