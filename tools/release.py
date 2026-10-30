#!/usr/bin/env python3
"""Release one addon (plans/RELEASE.md). `main` is protected: the release goes through a PR.

    python3 tools/release.py core 1.0.0          # normal release
    python3 tools/release.py memo 0.9.0          # < 1.0.0: published as a pre-release
    python3 tools/release.py core 2.0.0 --pre    # explicit pre-release (tag core-pre-v2.0.0)

Steps (nothing leaves this machine without asking):
  1. on an up-to-date, clean `main`;
  2. `## Unreleased` in <addon>/CHANGELOG.md has entries;
  3. branch `release/<addon>-<version>`: `## Unreleased` becomes `## <version> - <date>` (a new
     empty `## Unreleased` above it), V_* in <addon>/src/dllmain.cpp set, commit "<Addon> <version>";
  4. asks once, then: push the branch, open the PR, wait for CI (`test-and-build`), rebase-merge
     it, delete the branch, update local `main`, tag the merged commit `<addon>-v<version>`
     (or `<addon>-pre-v<version>`) and push the tag. The tag starts
     .github/workflows/release.yml, which publishes the DLL in the addon's release repository.
Answering "no" leaves the release commit on the local branch (how to undo is printed).

Needs git and an authenticated `gh`. Stdlib only.
"""

import argparse
import datetime
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADDONS = {
    "core": ("trailpack-core", "Trailpack Core"),
    "memo": ("trailpack-memo", "Trailpack Memo"),
}


def run(tool: str, *args: str, capture: bool = True) -> str:
    out = subprocess.run([tool, *args], cwd=ROOT, check=True, text=True,
                         capture_output=capture)
    return out.stdout.strip() if capture else ""


def git(*args: str, capture: bool = True) -> str:
    return run("git", *args, capture=capture)


def gh(*args: str, capture: bool = True) -> str:
    return run("gh", *args, capture=capture)


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
    ap = argparse.ArgumentParser(description="Release one addon through a PR into main and tag it.")
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
    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        fail("start on main (the release goes through a PR into main)")
    git("fetch", "-q", "origin", "main", "--tags")
    if git("rev-parse", "HEAD") != git("rev-parse", "origin/main"):
        fail("local main is not origin/main - run: git pull --ff-only")
    if git("tag", "-l", tag):
        fail(f"tag {tag} exists already")
    branch = f"release/{args.addon}-{args.version}"
    subject = f"{name} {args.version}"

    git("checkout", "-q", "-b", branch)
    bump_changelog(ROOT / folder / "CHANGELOG.md", args.version)
    set_version(ROOT / folder / "src" / "dllmain.cpp", args.version)
    git("add", f"{folder}/CHANGELOG.md", f"{folder}/src/dllmain.cpp")
    git("commit", "-q", "-m", subject)

    print(git("show", "--stat", "--format=%h %s", "HEAD"))
    kind = "pre-release" if args.pre or args.version.startswith("0.") else "release"
    print(f"\nRelease commit on {branch}; after the merge it gets the tag {tag} ({kind}).")
    answer = input("Push the branch, open the PR, merge it once CI is green and push the tag? [y/N] ")
    if answer.strip().lower() != "y":
        print(f"Nothing pushed. To start over: git checkout main && git branch -D {branch}")
        return

    git("push", "-q", "-u", "origin", branch, capture=False)
    notes = (f"Release {subject}: CHANGELOG section and version defines.\n\n"
             f"After the merge the tag `{tag}` starts the Release workflow.")
    gh("pr", "create", "--base", "main", "--head", branch, "--title", subject, "--body", notes, capture=False)
    print("Waiting for CI ...")
    for _ in range(30):                    # the checks show up a few seconds after the PR
        time.sleep(5)
        if subprocess.run(["gh", "pr", "checks", branch], cwd=ROOT, capture_output=True).returncode != 1:
            break
    try:
        gh("pr", "checks", branch, "--watch", "--fail-fast", capture=False)
    except subprocess.CalledProcessError:
        fail(f"CI failed - fix it on {branch} (push again), merge the PR, then tag by hand:\n"
             f"  git checkout main && git pull --ff-only && git tag -a {tag} -m '{subject}' <commit> "
             f"&& git push origin {tag}")
    gh("pr", "merge", branch, "--rebase", "--delete-branch", capture=False)

    git("checkout", "-q", "main")
    git("pull", "-q", "--ff-only", "origin", "main")
    git("branch", "-D", branch)
    # A rebase merge rewrites the commit: find it on main by its subject.
    commit = git("log", "-1", "--format=%H", "--fixed-strings", f"--grep={subject}", "origin/main")
    if not commit or git("log", "-1", "--format=%s", commit) != subject:
        fail(f"merged commit '{subject}' not found on main - tag it by hand: git tag -a {tag} <commit>")
    git("tag", "-a", tag, "-m", subject, commit)
    git("push", "-q", "origin", tag, capture=False)
    print(f"Tag {tag} pushed - the Release workflow builds and publishes it.")

if __name__ == "__main__":
    main()
