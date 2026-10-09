#!/usr/bin/env python3
"""Apply the native CRT overlay to the explicitly supported mpv revision."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
LOCK = json.loads((ROOT / "sources.lock.json").read_text())


def run(source, *args, check=True):
    # The explicitly selected dependency checkout may be bind-mounted from
    # another UID into the build container. Trust only this resolved path.
    return subprocess.run(["git", "-c", f"safe.directory={source}", "-C", str(source), *args], check=check,
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def apply(source):
    source = source.resolve()
    actual = run(source, "rev-parse", "HEAD").stdout.strip()
    if actual != LOCK["mpv"]:
        raise SystemExit(f"CRT requires mpv {LOCK['mpv']}; found {actual}. "
                         "Rebase and validate the overlay before changing the pin.")
    overlay = [src for src in (ROOT / "src").rglob("*") if src.is_file()]
    for src in overlay:
        dst = source / src.relative_to(ROOT / "src")
        if dst.exists() and dst.read_bytes() != src.read_bytes():
            raise SystemExit(f"Refusing to replace modified CRT source: {dst}")
    patch = str(ROOT / "integration.patch")
    forward = run(source, "apply", "--check", patch, check=False)
    if forward.returncode == 0:
        run(source, "apply", patch)
    elif run(source, "apply", "--reverse", "--check", patch, check=False).returncode:
        raise SystemExit("CRT patch conflicts with this source tree:\n" + forward.stderr)
    for src in overlay:
        dst = source / src.relative_to(ROOT / "src")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    shutil.copyfile(ROOT / "LICENSE-BlurBusters.txt", source / "LICENSE-BlurBusters.txt")
    print(f"Native CRT overlay applied to mpv {actual}.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    apply(parser.parse_args().source)
