"""Refuse files that look like game content, keys or generated game code.

The repository is public, so nothing from the game may ever land in it. This
check runs in CI on every push and pull request, and locally as the pre-commit
hook (.githooks/pre-commit).

    python tools/check_content.py           # every tracked or committable file
    python tools/check_content.py --staged  # only what is staged for the next commit

Standard library only, so it runs before any environment is set up.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Source files are small; anything bigger is almost certainly data.
MAX_BYTES = 1024 * 1024

# Switch containers and executables, plus the data formats of the game.
BLOCKED_SUFFIXES = {
    ".xci", ".xcz", ".nsp", ".nsz", ".nca", ".ncz", ".tik", ".cert", ".keys",
    ".nso", ".nro", ".nrr", ".npdm",
    ".trpak", ".trpfs", ".trpfd", ".bntx", ".bnsh", ".sarc", ".zs", ".webm",
}

# ExeFS modules carry no file extension.
BLOCKED_NAMES = {"main", "sdk", "rtld", "subsdk0", "subsdk1", "subsdk2", "main.npdm"}

# The code generator writes this marker into every file it emits. Split so this
# file does not carry the marker itself.
GENERATED_MARKER = "KARPUR_" + "GENERATED_FROM_GAME_CODE"

# A prod.keys / title.keys line: a name containing key, kek or seed, then hex.
KEY_LINE = re.compile(
    r"^\s*[A-Za-z0-9_]*(key|kek|seed)[A-Za-z0-9_]*\s*=\s*[0-9A-Fa-f]{32,}\s*$",
    re.MULTILINE | re.IGNORECASE,
)

# Paths that are allowed to exceed MAX_BYTES (for example vendored sources).
LARGE_OK_PREFIXES = ("third_party/",)


def list_files(staged: bool) -> list[Path]:
    if staged:
        cmd = ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"]
    else:
        # Tracked files plus untracked ones that are not ignored: everything a commit could pick up.
        cmd = ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    out = subprocess.run(cmd, check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def problems_for(path: Path) -> list[str]:
    found = []
    if path.suffix.lower() in BLOCKED_SUFFIXES:
        found.append(f"blocked file type {path.suffix}")
    if path.name.lower() in BLOCKED_NAMES:
        found.append("name of a Switch executable module")
    if not path.is_file():
        return found
    data = path.read_bytes()
    if len(data) > MAX_BYTES and not path.as_posix().startswith(LARGE_OK_PREFIXES):
        found.append(f"larger than {MAX_BYTES // 1024} KiB")
    if path.as_posix() != "tools/check_content.py" and GENERATED_MARKER.encode() in data:
        found.append("generated from game code")
    if data[:4] in (b"NSO0", b"PFS0", b"HFS0", b"NCA3"):
        found.append("Switch container or executable header")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return found
    if KEY_LINE.search(text):
        found.append("looks like a console key file")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--staged", action="store_true", help="check only the files staged for commit")
    args = parser.parse_args()

    bad = 0
    for path in list_files(args.staged):
        for problem in problems_for(path):
            print(f"REFUSED {path}: {problem}")
            bad += 1
    if bad:
        print(f"\n{bad} problem(s). Game files, keys and generated code must stay out of the repository.")
        return 1
    print("check_content: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
