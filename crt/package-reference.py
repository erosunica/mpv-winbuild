#!/usr/bin/env python3
"""Package the tested reference runtime, CRT configuration and provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--objdump", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    package = a.output / "mpv-x86_64-v3-crt-beam"
    package.mkdir(exist_ok=True)
    available = {f.name.lower(): f for f in a.runtime.glob("*.dll")}
    pending = [a.runtime / n for n in ("mpv.exe", "mpv.com", "libmpv-2.dll", "crt-math.exe", "crt-gpu.exe")]
    included, system = {}, set()
    while pending:
        f = pending.pop()
        if f.name.lower() in included:
            continue
        included[f.name.lower()] = f
        pe = subprocess.check_output([str(a.objdump), "-p", str(f)], text=True)
        for name in re.findall(r"DLL Name: ([^\r\n]+)", pe):
            key = name.strip().lower()
            if key in available:
                pending.append(available[key])
            else:
                system.add(key)
    for f in included.values():
        shutil.copyfile(f, package / f.name)
    shutil.copytree(ROOT / "config", package / "crt", dirs_exist_ok=True)
    shutil.copyfile(ROOT / "README.md", package / "crt/README.md")
    shutil.copyfile(ROOT / "LICENSE-BlurBusters.txt", package / "crt/LICENSE-BlurBusters.txt")
    shutil.copytree(ROOT / "docs", package / "crt/docs", dirs_exist_ok=True)
    for name in ("sources.lock.json", "reference-build.lock.json"):
        shutil.copyfile(ROOT / name, package / "crt" / name)
    manifest = {
        "target": "Windows x86_64-w64-mingw32 UCRT x86-64-v3",
        "sources": json.loads((ROOT / "sources.lock.json").read_text()),
        "dependencies": json.loads((ROOT / "reference-build.lock.json").read_text()),
        "system_imports": sorted(system),
        "native_crt_sources_sha256": {str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in (ROOT / "src").rglob("*") if f.is_file()},
        "integration_patch_sha256": hashlib.sha256((ROOT / "integration.patch").read_bytes()).hexdigest(),
        "runtime_sha256": {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in included.values()},
    }
    (package / "CRT-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Final zip is produced after licenses and validation results are copied.
    print(f"Prepared {package}: {len(included)} binaries, configuration and manifest.")


if __name__ == "__main__":
    main()
