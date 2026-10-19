#!/usr/bin/env python3
"""Compiles the GPU renderer's shaders (platform/gpu/shaders/*.slang) for every backend and embeds
them in C++ headers (platform/gpu/shaders/generated/*.h), which are committed: building Kompoz needs
none of the tools below, only changing a shader does.

Each shader is written once in Slang and compiled to
  SPIR-V (Vulkan), DXIL (Direct3D 12, through DXC), MSL (Metal), WGSL (WebGPU) and, from the SPIR-V
  through SPIRV-Cross, GLSL ES 3.00 (OpenGL ES 3 and WebGL 2).
The tools are downloaded at pinned versions (checked against their SHA-256) into
third_party/.tools/, SPIRV-Cross built from its source there. Linux x86-64 only: DXC and Slang's
releases are used as they come.

  scripts/shaders.py           compile and write the headers
  scripts/shaders.py --check   compile and fail if a header differs from what is committed (CI)
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "platform" / "gpu" / "shaders"
GENERATED = SOURCES / "generated"
TOOLS = ROOT / "third_party" / ".tools"

SLANG = ("2026.20",
         "https://github.com/shader-slang/slang/releases/download/v2026.20/slang-2026.20-linux-x86_64.tar.gz",
         "e863e568c204810d6e8bc31852f056aef6763c55ea9925db2db7affa060347c9")
DXC = ("1.9.2609",
       "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz",
       "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1")
SPIRV_CROSS = ("vulkan-sdk-1.4.363.0",
               "https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz",
               "f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721")

STAGES = {"vertex": "vs", "fragment": "fs", "compute": "cs"}


def fetch(name, spec):
    """The tool's directory, downloaded and unpacked once per version."""
    version, url, sha = spec
    dest = TOOLS / f"{name}-{version}"
    if (dest / ".complete").exists():
        return dest
    archive = TOOLS / Path(url).name
    TOOLS.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        print(f"{name} {version}: downloading {url}")
        part = archive.with_suffix(archive.suffix + ".part")
        with urllib.request.urlopen(url) as response, open(part, "wb") as out:
            shutil.copyfileobj(response, out)
        part.rename(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != sha:
        archive.unlink()
        sys.exit(f"{name}: checksum mismatch ({digest})")
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    with tarfile.open(archive) as tar:
        tar.extractall(dest, filter="data")
    (dest / ".complete").touch()
    return dest


def spirv_cross():
    """The spirv-cross command line tool, built from its pinned source."""
    src = fetch("SPIRV-Cross", SPIRV_CROSS)
    top = next(p for p in src.iterdir() if p.is_dir())
    build = src / "build"
    tool = build / "spirv-cross"
    if not tool.exists():
        print("SPIRV-Cross: building")
        subprocess.run(["cmake", "-S", str(top), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                        "-DSPIRV_CROSS_SHARED=OFF", "-DSPIRV_CROSS_ENABLE_TESTS=OFF", "-DSPIRV_CROSS_CLI=ON"],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["cmake", "--build", str(build), "--target", "spirv-cross", "-j", str(os.cpu_count() or 4)],
                       check=True, stdout=subprocess.DEVNULL)
    return tool


def entry_points(source):
    """(stage, entry name) for each [shader("...")] function in a Slang file."""
    text = source.read_text()
    return [(stage, name) for stage, name in
            re.findall(r'\[shader\("(\w+)"\)\]\s*(?:\[[^\]]*\]\s*)*[\w<>, ]+?\s+(\w+)\s*\(', text)]


def compile_shader(source, stage, entry, tools, work):
    """{target: bytes} for one entry point."""
    slangc, dxc_lib, cross = tools
    base = work / f"{source.stem}-{entry}"
    env = dict(os.environ, LD_LIBRARY_PATH=f"{dxc_lib}:{os.environ.get('LD_LIBRARY_PATH', '')}")

    def run(command):
        result = subprocess.run(command, env=env, capture_output=True)
        if result.returncode != 0:
            sys.exit(f"{source.name} ({entry}): {Path(command[0]).name} failed\n{result.stderr.decode()}{result.stdout.decode()}")
        return result.stdout

    def slang(target, suffix, *extra):
        out = Path(f"{base}.{suffix}")
        run([str(slangc), str(source), "-entry", entry, "-stage", stage, "-target", target, "-O2",
             "-matrix-layout-row-major", "-o", str(out), *extra])
        return out.read_bytes()

    out = {"spirv": slang("spirv", "spv", "-DKOMPOZ_SPIRV", "-fvk-use-entrypoint-name"),  # (named as on the other targets)
           "dxil": slang("dxil", "dxil", "-profile", "sm_6_0"),
           "msl": slang("metal", "metal"),
           "wgsl": slang("wgsl", "wgsl")}
    if stage != "compute":  # OpenGL ES 3.0 and WebGL 2 have no compute shaders
        # (What SPIR-V doesn't mark relaxed comes out highp.)
        out["glsl"] = run([str(cross), "--es", "--version", "300", f"{base}.spv"])
    return out


def embed(name, data, text):
    """A C++ array of the bytes (a text one ends in a NUL)."""
    if text:
        data = data + b"\0"
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    body = ",\n    ".join(rows)
    return f"inline constexpr unsigned char {name}[{len(data)}] = {{\n    {body}}};\n"


def header(source, compiled):
    lines = [f"// Generated by scripts/shaders.py from {source.name}: don't edit, run the script.\n",
             "#pragma once\n\n",
             "namespace kompoz::gpu::shaders {\n\n"]
    for (stage, entry), targets in compiled:
        for target, data in targets.items():
            lines.append(embed(f"{source.stem}_{entry}_{target}", data, target in ("msl", "wgsl", "glsl")))
        lines.append("\n")
    lines.append("} // namespace kompoz::gpu::shaders\n")
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if a committed header is out of date")
    args = parser.parse_args()
    if sys.platform != "linux" or os.uname().machine != "x86_64":
        sys.exit("scripts/shaders.py runs on Linux x86-64 (where Slang's and DXC's releases run)")

    slang = fetch("slang", SLANG)
    dxc = fetch("dxc", DXC)
    tools = (slang / "bin" / "slangc", dxc / "lib", spirv_cross())
    work = TOOLS / "shader-build"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)

    stale = []
    expected = set()
    for source in sorted(SOURCES.glob("*.slang")):
        compiled = [((stage, entry), compile_shader(source, stage, entry, tools, work))
                    for stage, entry in entry_points(source)]
        if not compiled:
            continue
        target = GENERATED / f"{source.stem}.h"
        expected.add(target)
        text = header(source, compiled)
        if args.check:
            if not target.exists() or target.read_text() != text:
                stale.append(target)
        elif not target.exists() or target.read_text() != text:
            GENERATED.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
            print(f"wrote {target.relative_to(ROOT)}")
    orphans = [p for p in GENERATED.glob("*.h") if p not in expected] if GENERATED.exists() else []
    if args.check:
        if stale or orphans:
            for p in stale + orphans:
                print(f"out of date: {p.relative_to(ROOT)}", file=sys.stderr)
            sys.exit("run scripts/shaders.py and commit the headers")
        print("shaders: up to date")
    else:
        for p in orphans:
            p.unlink()
            print(f"removed {p.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
