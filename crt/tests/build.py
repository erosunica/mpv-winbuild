#!/usr/bin/env python3
"""Check actual patch application, idempotence and rejection of altered inputs."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]


def run(*args, success=True):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    assert (p.returncode == 0) == success, (args, p.stdout, p.stderr)
    return p.stdout


def test(mpv, recipes):
    with tempfile.TemporaryDirectory(prefix="crt-build-test-") as tmp:
        tmp = Path(tmp)
        source = tmp / "mpv"
        run("git", "clone", "--shared", "--no-checkout", mpv, source)
        revision = run("git", "-C", mpv, "rev-parse", "HEAD").strip()
        run("git", "-C", source, "checkout", "--detach", revision)
        run("python3", ROOT / "apply.py", source)
        first = run("git", "-C", source, "diff")
        assert "crt-beam-scans" in first and "crt_beam.c" in first
        run("python3", ROOT / "apply.py", source)
        assert run("git", "-C", source, "diff") == first
        modified = source / "video/out/gpu_next/crt_beam.c"
        modified.write_text(modified.read_text() + "/* local change */\n")
        run("python3", ROOT / "apply.py", source, success=False)
        run("git", "-C", source, "-c", "user.name=CRT test", "-c",
            "user.email=crt-test@example.invalid", "commit", "--allow-empty", "-m", "Wrong revision fixture")
        run("python3", ROOT / "apply.py", source, success=False)

        out = tmp / "recipes/packages"
        out.mkdir(parents=True)
        for name in ("mpv", "libplacebo"):
            shutil.copyfile(recipes / "packages" / f"{name}.cmake", out / f"{name}.cmake")
        run("python3", ROOT / "prepare-build.py", out.parent)
        first = (out / "mpv.cmake").read_text()
        assert "PATCH_COMMAND python3" in first and "GIT_RESET" in first
        assert "/mpv-package/crt" in first
        run("python3", ROOT / "prepare-build.py", out.parent)
        assert (out / "mpv.cmake").read_text() == first
    print("CRT overlay: clean application, idempotence, changed-file/revision rejection and packaging passed.")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mpv", type=Path)
    p.add_argument("recipes", type=Path)
    args = p.parse_args()
    test(args.mpv, args.recipes)
