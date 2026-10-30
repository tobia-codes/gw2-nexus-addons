#!/usr/bin/env python3
"""Prepare a release of one addon (plans/RELEASE.md).

    python3 tools/release.py core 1.0.0          # normal release
    python3 tools/release.py memo 0.9.0          # < 1.0.0: published as a pre-release
    python3 tools/release.py core 2.0.0 --pre    # explicit pre-release (tag core-pre-v2.0.0)

Steps (nothing is pushed without asking):
  1. the working tree is clean and on a branch that is pushed later;
  2. `## Unreleased` in <addon>/CHANGELOG.md has entries;
  3. `## Unreleased` becomes `## <version> - <date>`, a new empty `## Unreleased` goes above it;
  4. V_MAJOR / V_MINOR / V_BUILD / V_REVISION in <addon>/src/dllmain.cpp are set;
  5. commit "<Addon> <version>" and tag `<addon>-v<version>` (or `<addon>-pre-v<version>`);
  6. shows both and asks before `git push` of the commit and the tag. The tag starts
     .github/workflows/release.yml, which publishes the DLL in the addon's release repository.

Stdlib only.
"""

import argparse
import datetime
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDONS = {
    "core": ("trailpack-core", "Trailpack Core"),
    "memo": ("trailpack-memo", "Trailpack Memo"),
}


def git(*args: str, capture: bool = True) -> str:
    out = subprocess.run(["git", *args], cwd=ROOT, check=True, text=True,
                         capture_output=capture)
    return out.stdout.strip() if capture else ""


def fail(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def bump_changelog(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    m = re.search(r"^## Unreleased[ \t]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        fail(f"{path.relative_to(ROOT)} has no '## Unreleased' section")
    if not m.group(1).strip():
        fail(f"'## Unreleased' in {path.relative_to(ROOT)} is empty - nothing to release")
    if re.search(rf"^## {re.escape(version)}\b", text, re.M):
        fail(f"{path.relative_to(ROOT)} already has a section for {version}")
    date = datetime.date.today().isoformat()
    text = text[:m.start()] + f"## Unreleased\n\n## {version} - {date}\n" + m.group(1) + text[m.end():]
    path.write_text(text, encoding="utf-8")


def set_version(path: Path, version: str) -> None:
    parts = [int(p) for p in version.split(".")] + [0]
    text = path.read_text(encoding="utf-8")
    for name, value in zip(("V_MAJOR", "V_MINOR", "V_BUILD", "V_REVISION"), parts[:4]):
        text, n = re.subn(rf"^(#define {name}\s+)\d+", rf"\g<1>{value}", text, flags=re.M)
        if n != 1:
            fail(f"{name} not found once in {path.relative_to(ROOT)}")
    path.write_text(text, encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description="Prepare and tag a release of one addon.")
    ap.add_argument("addon", choices=sorted(ADDONS))
    ap.add_argument("version", help="MAJOR.MINOR.PATCH, e.g. 1.0.0")
    ap.add_argument("--pre", action="store_true", help="publish as a pre-release (tag <addon>-pre-v...)")
    args = ap.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        fail("version must be MAJOR.MINOR.PATCH (Nexus ignores tags with suffixes like -beta)")
    folder, name = ADDONS[args.addon]
    tag = f"{args.addon}-{'pre-' if args.pre else ''}v{args.version}"

    if git("status", "--porcelain"):
        fail("working tree is not clean - commit or stash first")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    if git("tag", "-l", tag):
        fail(f"tag {tag} exists already")

    bump_changelog(ROOT / folder / "CHANGELOG.md", args.version)
    set_version(ROOT / folder / "src" / "dllmain.cpp", args.version)
    git("add", f"{folder}/CHANGELOG.md", f"{folder}/src/dllmain.cpp")
    git("commit", "-q", "-m", f"{name} {args.version}")
    git("tag", "-a", tag, "-m", f"{name} {args.version}")

    print(git("show", "--stat", "--format=%h %s", "HEAD"))
    kind = "pre-release" if args.pre or args.version.startswith("0.") else "release"
    print(f"\nTag {tag} ({kind}) on branch {branch}.")
    answer = input(f"Push branch {branch} and tag {tag} now? [y/N] ").strip().lower()
    if answer != "y":
        print(f"Not pushed. Later: git push origin {branch} && git push origin {tag}\n"
              f"Undo: git tag -d {tag} && git reset --hard HEAD~1")
        return
    git("push", "origin", branch, capture=False)
    git("push", "origin", tag, capture=False)
    print("Pushed - the Release workflow builds and publishes it.")


if __name__ == "__main__":
    main()
