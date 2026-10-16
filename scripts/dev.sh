#!/usr/bin/env bash
# Rebuilds and restarts an example whenever a source file changes, keeping its saved state (the
# screen it is on, scroll positions, rememberSaveable values) across the restart through
# KOMPOZ_STATE_FILE. A build that fails leaves the running app as it is.
#
#   scripts/dev.sh                     # the showcase, built in build/
#   scripts/dev.sh showcase build kompoz://components
#
# Arguments: the target (showcase), the build directory (build) and what the app is started with.
# Closing the app's window ends it, as does Ctrl+C.
set -u
# A sanitizer build reports at exit a few bytes from a library already unloaded (the GL driver):
# noise on every restart here.
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}

target=${1:-showcase}
build=${2:-build}
shift $(($# < 2 ? $# : 2))
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
[ -f "$build/CMakeCache.txt" ] || { echo "dev.sh: no CMake build in $build (cmake -S . -B $build)" >&2; exit 1; }
app="$build/$target"
state="$build/$target.state"
rm -f "$state"
pid=

# The newest change among the sources (the library, its platforms and the examples; not build output).
stamp() {
    find include src platform examples CMakeLists.txt \
        \( -path '*/build' -o -path '*/.gradle' -o -path '*/.cxx' \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name 'CMakeLists.txt' -o -name '*.cmake' \) \
        -printf '%T@\n' 2>/dev/null | sort -n | tail -1
}

stop() {
    [ -n "$pid" ] || return
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid"  # SDL turns it into a quit: the app writes its state on the way out
        wait "$pid" 2>/dev/null
    fi
    pid=
}

start() {
    KOMPOZ_STATE_FILE="$state" "$app" "$@" &
    pid=$!
}

build() {
    local began=$(date +%s%N) out status
    out=$(cmake --build "$build" --target "$target" -j"$(nproc)" 2>&1)
    status=$?
    grep -E 'error|warning:' <<<"$out" >&2
    if [ "$status" -eq 0 ]; then
        echo "dev.sh: built in $(( ($(date +%s%N) - began) / 1000000 )) ms" >&2
    else
        echo "dev.sh: the build failed" >&2
    fi
    return "$status"
}

trap 'stop; rm -f "$state"; exit 0' INT TERM

build || exit 1
start "$@"
last=$(stamp)
while true; do
    sleep 0.5
    if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then  # its window closed
        wait "$pid" 2>/dev/null
        rm -f "$state"
        exit 0
    fi
    now=$(stamp)
    [ "$now" != "$last" ] || continue
    last=$now
    echo "dev.sh: changed, building..." >&2
    if build; then
        stop
        start "$@"
    else
        echo "dev.sh: the app keeps running" >&2
    fi
done
