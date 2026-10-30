#!/usr/bin/env bash
# Generates platform/linux/wayland/generated/ from the Wayland protocols in
# platform/linux/wayland/protocols/ (the core's and the extensions Kompoz uses, as SDL's sources had
# them) with wayland-scanner: each one's client header (on wayland-client-core.h alone, so that the
# system's wayland-client-protocol.h never comes in beside ours) and its interfaces (private code:
# hidden in Kompoz, the core's included, so that libwayland-client, loaded at run time, needn't
# give any). The generated files are committed: building Kompoz needs no wayland-scanner.
# Usage: scripts/wayland-protocols.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
in="$root/platform/linux/wayland/protocols"
out="$root/platform/linux/wayland/generated"
rm -rf "$out"
mkdir -p "$out"
for xml in "$in"/*.xml; do
    name=$(basename "$xml" .xml)
    [[ $name == wayland ]] && header=wayland-client-protocol.h || header=$name-client-protocol.h
    wayland-scanner --include-core-only client-header "$xml" "$out/$header"
    wayland-scanner private-code "$xml" "$out/$name-protocol.c"
done
echo "generated $(ls "$out" | wc -l) files with $(wayland-scanner --version 2>&1)"
