#!/usr/bin/env bash
# Packages the showcase for every platform it builds on, into dist/:
#
#   kompoz-showcase-<version>-linux-x86_64.tar.gz   Release; the system's FreeType, HarfBuzz, libpng,
#                                                    libcurl, libwebp and libturbojpeg (as built here)
#   kompoz-showcase-<version>-windows-x86_64.zip    MinGW cross build, static: Windows 10 and later
#   kompoz-showcase-<version>-android.apk           debug-signed: adb install, or open it on the phone
#   kompoz-showcase-<version>-web.zip               Emscripten: serve the folder over HTTP
#
#   scripts/package.sh [linux] [windows] [android] [web]    (none: all of them)
#
# Each needs its toolchain and scripts/fetch-deps.sh (see README); one that can't be built is
# reported and skipped. The build directories are build-release, build-windows and build-web
# (configured on first use) and the Android project's own.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
# The release's version (CMakeLists.txt's); off its v<version> tag, or with changes not committed,
# the commit it was built from too.
version=$(sed -n 's/^project(kompoz VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
if [ "$(git describe --exact-match --tags 2>/dev/null)" != "v$version" ] || [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    version="$version-$(git describe --always --dirty 2>/dev/null || echo dev)"
fi
dist="$root/dist"
mkdir -p "$dist"
jobs=$(nproc)
wanted=${*:-linux windows android web}
failed=()
# What apps ship (assets/licenses) has to be what the repository says.
cmp -s LICENSE assets/licenses/Kompoz.txt && cmp -s NOTICE assets/licenses/NOTICE.txt ||
    { echo "package.sh: assets/licenses/Kompoz.txt and NOTICE.txt differ from LICENSE and NOTICE" >&2; exit 1; }

note() { echo "package.sh: $*" >&2; }

# The showcase and the files it reads next to it (fonts, images, vectors), copied by the build.
stage() {  # stage <build dir> <binary> <strip> <staging dir>
    local build=$1 binary=$2 strip=$3 out=$4
    rm -rf "$out"
    mkdir -p "$out"
    cp "$build/$binary" "$out/"
    $strip "$out/$binary"
    for dir in fonts licenses images vectors; do
        [ -d "$build/$dir" ] && cp -r "$build/$dir" "$out/"
    done
}

linux() {
    local build=build-release name="kompoz-showcase-$version-linux-x86_64"
    [ -f $build/CMakeCache.txt ] || cmake -S . -B $build -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF > /dev/null || return 1
    cmake --build $build --target showcase -j"$jobs" > /dev/null || return 1
    stage $build showcase strip "$dist/$name"
    cat > "$dist/$name/README.txt" <<EOF
Kompoz showcase ($version) for Linux x86_64.
Run ./showcase (in a Wayland session or on an X11 display). It uses the system's FreeType, HarfBuzz,
libpng, libcurl, libwebp and libturbojpeg.
A deep link opens a screen: ./showcase kompoz://components
EOF
    tar -C "$dist" -czf "$dist/$name.tar.gz" "$name" && rm -rf "$dist/${name:?}"
}

windows() {
    local build=build-windows name="kompoz-showcase-$version-windows-x86_64"
    command -v x86_64-w64-mingw32-g++ > /dev/null || { note "no MinGW-w64"; return 1; }
    [ -f $build/CMakeCache.txt ] || cmake -S . -B $build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
        -DCMAKE_BUILD_TYPE=Release > /dev/null || return 1
    cmake --build $build --target showcase -j"$jobs" > /dev/null || return 1
    stage $build showcase.exe x86_64-w64-mingw32-strip "$dist/$name"
    printf 'Kompoz showcase (%s) for Windows 10 and later (x86_64).\r\nRun showcase.exe; keep the fonts, licenses, images and vectors folders next to it.\r\n' \
        "$version" > "$dist/$name/README.txt"
    (cd "$dist" && rm -f "$name.zip" && python3 -m zipfile -c "$name.zip" "$name") && rm -rf "$dist/${name:?}"
}

android() {
    local project=examples/showcase/android name="kompoz-showcase-$version-android.apk"
    local jbr=~/.local/share/JetBrains/Toolbox/apps/android-studio/jbr
    [ -n "${JAVA_HOME:-}" ] || { [ -d "$jbr" ] && export JAVA_HOME=$jbr; }
    # Packaged afresh: a debug APK repackaged in place keeps the space of what it dropped (~15 MB).
    rm -f $project/app/build/outputs/apk/debug/app-debug.apk
    (cd $project && ./gradlew --offline -q assembleDebug) || return 1
    cp $project/app/build/outputs/apk/debug/app-debug.apk "$dist/$name"
}

web() {
    local build=build-web name="kompoz-showcase-$version-web"
    command -v emcmake > /dev/null || [ -x /usr/lib/emscripten/emcmake ] || { note "no Emscripten"; return 1; }
    local emcmake=emcmake
    command -v emcmake > /dev/null || emcmake=/usr/lib/emscripten/emcmake
    [ -f $build/CMakeCache.txt ] || $emcmake cmake -S . -B $build -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF > /dev/null || return 1
    cmake --build $build --target showcase -j"$jobs" > /dev/null || return 1
    rm -rf "$dist/$name"
    mkdir -p "$dist/$name"
    cp $build/showcase.html $build/showcase.js $build/showcase.wasm $build/showcase.data "$dist/$name/"
    cp "$dist/$name/showcase.html" "$dist/$name/index.html"
    cat > "$dist/$name/README.txt" <<EOF
Kompoz showcase ($version) for the web. Browsers don't run WebAssembly from file:// pages:
serve this folder, e.g. python3 -m http.server 8765, then open http://localhost:8765/
EOF
    (cd "$dist" && rm -f "$name.zip" && python3 -m zipfile -c "$name.zip" "$name") && rm -rf "$dist/${name:?}"
}

for platform in $wanted; do
    case $platform in
        linux | windows | android | web)
            note "$platform..."
            if ! $platform; then
                note "$platform: not packaged"
                failed+=("$platform")
            fi
            ;;
        *) note "unknown platform: $platform (linux, windows, android, web)"; failed+=("$platform") ;;
    esac
done
ls -lh "$dist" | grep "$version" >&2
[ ${#failed[@]} -eq 0 ]
