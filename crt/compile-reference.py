#!/usr/bin/env python3
"""Rebuild the Windows v3 reference player with an existing Windows dependency prefix.

For initial dependency installation use the normal mpv-winbuild recipes.
reference-build.lock.json records the dependencies used for the cloud artifact.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--toolchain", type=Path, required=True)
    p.add_argument("--prefix", type=Path, required=True)
    p.add_argument("--work", type=Path, required=True)
    p.add_argument("--jobs", type=int, default=3)
    a = p.parse_args()
    source, tc, prefix, work = [x.resolve() for x in (a.source, a.toolchain, a.prefix, a.work)]
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(tc / "bin") + os.pathsep + env["PATH"]
    env["PKG_CONFIG_LIBDIR"] = str(prefix / "lib/pkgconfig")

    def run(*args):
        print(shlex.join(map(str, args)), flush=True)
        subprocess.run(list(map(str, args)), env=env, check=True)

    for dep, version in {"libplacebo": "7.374.0", "libavcodec": "62.11.100",
                         "libass": "0.17.4"}.items():
        actual = subprocess.check_output(["pkg-config", "--modversion", dep], env=env, text=True).strip()
        if actual != version:
            raise SystemExit(f"Reference build requires {dep} {version}; found {actual}")
    run("python3", ROOT / "apply.py", source)
    cross = work / "reference-cross.ini"
    target = "x86_64-w64-mingw32"
    binaries = {"c": "clang", "cpp": "clang++", "ar": "ar", "strip": "strip", "windres": "windres"}
    def quoted(path):
        value = str(path)
        if "'" in value or "\n" in value or "\r" in value:
            raise SystemExit("Meson machine file paths must not contain quotes or newlines")
        return "'" + value.replace("\\", "\\\\") + "'"

    text = "[binaries]\n" + "\n".join(
        f"{key} = {quoted(tc / 'bin' / (target + '-' + value))}"
        for key, value in binaries.items())
    text += f"\npkg-config = {quoted(shutil.which('pkg-config'))}\n"
    text += "[host_machine]\nsystem = 'windows'\ncpu_family = 'x86_64'\ncpu = 'x86_64'\nendian = 'little'\n"
    text += f"[properties]\nneeds_exe_wrapper = true\npkg_config_libdir = [{quoted(prefix / 'lib/pkgconfig')}]\n"
    text += "[built-in options]\nc_args = ['-march=x86-64-v3']\ncpp_args = ['-march=x86-64-v3']\nbuildtype = 'release'\n"
    cross.write_text(text)
    build = work / "mpv-reference"
    args = ["meson", "setup", build, source, f"--cross-file={cross}",
            f"--prefix={prefix}", "--libdir=lib", "-Dlibmpv=true", "-Dbuild-date=false",
            "-Dlua=enabled", "-Djavascript=disabled", "-Dmanpage-build=disabled",
            "-Dd3d11=enabled", "-Dvulkan=disabled", "-Dgl=disabled", "-Dc_link_args=['-lc++']"]
    for name in ("libarchive", "lcms2", "libbluray", "dvdnav", "uchardet", "rubberband", "vapoursynth", "libcurl"):
        args.append(f"-D{name}=disabled")
    if (build / "meson-private/coredata.dat").exists():
        args += ["--reconfigure", "--clearcache"]
    run(*args)
    run("ninja", "-C", build, f"-j{a.jobs}")
    runtime = work / "reference-runtime"
    runtime.mkdir(exist_ok=True)
    for name in ("mpv.exe", "mpv.com", "libmpv-2.dll"):
        shutil.copyfile(build / name, runtime / name)
    for directory in (prefix / "bin", tc / target / "bin"):
        for dll in directory.glob("*.dll"):
            shutil.copyfile(dll, runtime / dll.name)
    run(tc / "bin" / f"{target}-clang", "-march=x86-64-v3", f"-I{ROOT / 'src'}",
        ROOT / "tests/math.c", "-lm", "-o", runtime / "crt-math.exe")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "libplacebo"], env=env, text=True))
    run(tc / "bin" / f"{target}-clang", "-D_GNU_SOURCE", "-DTA_MEMORY_DEBUGGING=0",
        "-DCRT_TEST_D3D11", "-std=gnu11", "-O2", "-march=x86-64-v3",
        f"-I{source}", f"-I{build}", ROOT / "tests/gpu.c",
        source / "video/out/gpu_next/crt_beam.c", source / "ta/ta.c",
        source / "ta/ta_utils.c", source / "ta/ta_talloc.c", *flags, "-lc++",
        "-o", runtime / "crt-gpu.exe")
    print(f"Windows v3 reference player and test executables: {runtime}")


if __name__ == "__main__":
    main()
