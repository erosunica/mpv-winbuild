#!/usr/bin/env python3
"""Configure upstream CMake recipes to build and package the native CRT port."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LOCK = json.loads((ROOT / "sources.lock.json").read_text())
MARKER = "# mpv-winbuild native CRT integration"


def prepare(recipes):
    recipes = recipes.resolve()
    for name in ("mpv", "libplacebo"):
        path = recipes / "packages" / f"{name}.cmake"
        text = path.read_text()
        if MARKER in text:
            continue
        anchor = "    GIT_REPOSITORY "
        if text.count(anchor) != 1:
            raise SystemExit(f"Unrecognized recipe: {path}")
        # GIT_RESET is used by the recipes' force-update/fullclean steps too.
        text = text.replace(anchor, f"    {MARKER}\n"
                            f"    GIT_TAG {LOCK[name]}\n"
                            f"    GIT_REMOTE_NAME origin\n"
                            f"    GIT_RESET {LOCK[name]}\n" + anchor, 1)
        if name == "mpv":
            # Optional PR patching also uses PATCH_COMMAND. Refuse silently
            # dropping it; the CRT baseline must remain reviewable and tested.
            if "PATCH_COMMAND" in text:
                raise SystemExit("CRT baseline cannot be combined with unvalidated mpv PR patches.")
            text = text.replace('    UPDATE_COMMAND ""',
                f'    PATCH_COMMAND python3 "{ROOT / "apply.py"}" <SOURCE_DIR>\n'
                '    UPDATE_COMMAND ""', 1)
            anchor = '    COMMENT "Copying mpv binaries and manual"'
            if text.count(anchor) != 1:
                raise SystemExit("Missing mpv packaging step")
            text = text.replace(anchor,
                f'    COMMAND ${{CMAKE_COMMAND}} -E copy_directory "{ROOT / "config"}" '
                '${CMAKE_CURRENT_BINARY_DIR}/mpv-package/crt\n'
                f'    COMMAND ${{CMAKE_COMMAND}} -E copy "{ROOT / "README.md"}" '
                '${CMAKE_CURRENT_BINARY_DIR}/mpv-package/crt/README.md\n'
                f'    COMMAND ${{CMAKE_COMMAND}} -E copy "{ROOT / "LICENSE-BlurBusters.txt"}" '
                '${CMAKE_CURRENT_BINARY_DIR}/mpv-package/crt/LICENSE-BlurBusters.txt\n' + anchor, 1)
        path.write_text(text)
    print("CRT build recipes prepared with pinned mpv and libplacebo revisions.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipes", type=Path)
    prepare(parser.parse_args().recipes)
