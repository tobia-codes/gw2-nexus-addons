#!/usr/bin/env python3
"""Changesets for the Trailpack addons (plans/RELEASE.md) — the Changesets idea and file format
(https://github.com/changesets/changesets) without Node.

Every player-visible change adds a file `.changeset/<name>.md`:

    ---
    "core": minor
    "memo": patch
    ---

    Text for players (English), becomes the release note.

Bumps: major / minor / patch. When `develop` is merged into `main`, the release workflow asks
this script which addons have pending changesets, computes each new version from the addon's
last release tag (`<addon>-v<version>`) plus the highest pending bump (no tag yet: the
`initialVersion` from `.changeset/config.json`), and releases them. A changeset is pending for an
addon while it did not exist yet at that addon's last release tag. Afterwards `apply` writes the
CHANGELOG sections and deletes the consumed files (on `develop`).

    python3 tools/changeset.py add minor core "Item search in the options."
    python3 tools/changeset.py status                 # what a merge to main would release
    python3 tools/changeset.py plan --json plan.json  # CI: plan file, then from it:
    python3 tools/changeset.py show plan.json summary|cmake|tsv|count
    python3 tools/changeset.py apply plan.json        # CI, on develop

Versions below 1.0.0 are published as pre-releases. Stdlib only.
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIR = ".changeset"
BUMPS = ("patch", "minor", "major")


def git(*args: str, root: Path) -> tuple[int, str]:
    p = subprocess.run(["git", *args], cwd=root, text=True, capture_output=True)
    return p.returncode, p.stdout.strip()


def config(root: Path) -> dict:
    return json.loads((root / DIR / "config.json").read_text(encoding="utf-8"))["addons"]


def parse_version(v: str) -> tuple[int, int, int]:
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", v)
    if not m:
        raise ValueError(f"not MAJOR.MINOR.PATCH: {v}")
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def bump(version: str, kind: str) -> str:
    major, minor, patch = parse_version(version)
    if kind == "major":
        return f"{major + 1}.0.0"
    if kind == "minor":
        return f"{major}.{minor + 1}.0"
    return f"{major}.{minor}.{patch + 1}"


def parse_changeset(text: str) -> tuple[dict, str]:
    """({addon: bump}, summary) of one changeset file; ValueError if malformed."""
    m = re.match(r"\s*---\s*\n(.*?)\n---\s*\n?(.*)", text, re.S)
    if not m:
        raise ValueError("missing '---' front matter")
    bumps = {}
    for line in m.group(1).splitlines():
        if not line.strip():
            continue
        lm = re.fullmatch(r"\s*[\"']?([\w-]+)[\"']?\s*:\s*(\w+)\s*", line)
        if not lm or lm.group(2) not in BUMPS:
            raise ValueError(f"bad line '{line}' (expected '\"addon\": major|minor|patch')")
        bumps[lm.group(1)] = lm.group(2)
    summary = m.group(2).strip()
    if not bumps or not summary:
        raise ValueError("needs at least one addon and a text")
    return bumps, summary


def changeset_files(root: Path) -> list[Path]:
    return sorted(p for p in (root / DIR).glob("*.md") if p.name.lower() != "readme.md")


def last_release(root: Path, addon: str) -> tuple[str, str] | None:
    """(version, tag) of the addon's highest release tag, or None."""
    _, out = git("tag", "-l", f"{addon}-v*", root=root)
    best = None
    for tag in out.splitlines():
        try:
            v = parse_version(tag[len(addon) + 2:])
        except ValueError:
            continue
        if best is None or v > best[0]:
            best = (v, tag)
    return (".".join(map(str, best[0])), best[1]) if best else None


def plan(root: Path) -> list[dict]:
    addons = config(root)
    files = changeset_files(root)
    parsed = {}
    for f in files:
        try:
            parsed[f] = parse_changeset(f.read_text(encoding="utf-8"))
        except ValueError as e:
            raise SystemExit(f"error: {f.relative_to(root)}: {e}")
        unknown = set(parsed[f][0]) - set(addons)
        if unknown:
            raise SystemExit(f"error: {f.relative_to(root)}: unknown addon(s) {', '.join(sorted(unknown))}")

    releases = []
    for addon, cfg in addons.items():
        last = last_release(root, addon)
        pending = []
        for f, (bumps, summary) in parsed.items():
            if addon not in bumps:
                continue
            rel = f.relative_to(root).as_posix()
            if last and git("cat-file", "-e", f"{last[1]}:{rel}", root=root)[0] == 0:
                continue                                    # already released with that tag
            pending.append((rel, bumps[addon], summary))
        if not pending:
            continue
        kind = max((b for _, b, _ in pending), key=BUMPS.index)
        version = bump(last[0], kind) if last else cfg["initialVersion"]
        sections = []
        for k, title in (("major", "Major changes"), ("minor", "Minor changes"), ("patch", "Patch changes")):
            items = [s for _, b, s in pending if b == k]
            if items:
                sections.append(f"### {title}\n\n" + "\n".join(
                    "- " + s.replace("\n", "\n  ") for s in items))
        releases.append({
            "addon": addon,
            "name": cfg["name"],
            "folder": cfg["folder"],
            "target": cfg["target"],
            "releaseRepo": cfg["releaseRepo"],
            "version": version,
            "previous": last[0] if last else None,
            "bump": kind,
            "prerelease": parse_version(version)[0] == 0,
            "tag": f"{addon}-v{version}",
            "notes": "\n\n".join(sections),
            "files": [p for p, _, _ in pending],
        })
    return releases


def apply(root: Path, releases: list[dict], date: str | None = None) -> None:
    """Write the CHANGELOG sections and delete changesets that every named addon has consumed."""
    date = date or datetime.date.today().isoformat()
    for r in releases:
        path = root / r["folder"] / "CHANGELOG.md"
        text = path.read_text(encoding="utf-8") if path.exists() else f"# Changelog — {r['name']}\n"
        if re.search(rf"^## {re.escape(r['version'])}\b", text, re.M):
            continue                                        # already written
        section = f"## {r['version']} - {date}\n\n{r['notes']}\n\n"
        m = re.search(r"^## ", text, re.M)
        text = text[:m.start()] + section + text[m.start():] if m else text.rstrip("\n") + "\n\n" + section
        path.write_text(text.rstrip("\n") + "\n", encoding="utf-8")
    released = {r["addon"] for r in releases}
    for f in {f for r in releases for f in r["files"]}:
        p = root / f
        if not p.exists():
            continue
        bumps, _ = parse_changeset(p.read_text(encoding="utf-8"))
        if set(bumps) <= released:
            p.unlink()


def show(releases: list[dict], fmt: str, notes_dir: Path | None = None) -> str:
    """The plan as text for the workflow: markdown summary, CMake arguments, one TSV line per
    release (tag, version, target, release repo, name, folder, prerelease; notes files written to
    `notes_dir/<tag>.md`) or the number of releases."""
    if fmt == "count":
        return str(len(releases))
    if fmt == "cmake":
        return " ".join(f"-DTRAILPACK_VERSION_{r['target']}={r['version']}" for r in releases)
    if fmt == "summary":
        lines = [f"- {r['name']}: {r['previous'] or '(first)'} -> {r['version']} "
                 f"({r['bump']}, {'pre-release' if r['prerelease'] else 'release'})" for r in releases]
        return "\n".join(lines) if lines else "Nothing to release."
    if fmt == "tsv":
        rows = []
        for r in releases:
            if notes_dir is not None:
                (notes_dir / f"{r['tag']}.md").write_text(r["notes"] + "\n", encoding="utf-8")
            rows.append("\t".join([r["tag"], r["version"], r["target"], r["releaseRepo"], r["name"],
                                   r["folder"], "true" if r["prerelease"] else "false"]))
        return "\n".join(rows)
    raise ValueError(f"unknown format {fmt}")


def add(root: Path, kind: str, addons: list[str], summary: str) -> Path:
    known = config(root)
    for a in addons:
        if a not in known:
            raise SystemExit(f"error: unknown addon {a} (known: {', '.join(known)})")
    if kind not in BUMPS:
        raise SystemExit("error: bump must be major, minor or patch")
    slug = re.sub(r"[^a-z0-9]+", "-", summary.lower()).strip("-")[:40] or "change"
    path = root / DIR / f"{datetime.date.today().isoformat()}-{slug}.md"
    n = 2
    while path.exists():
        path = path.with_name(f"{path.stem}-{n}.md")
        n += 1
    head = "\n".join(f'"{a}": {kind}' for a in addons)
    path.write_text(f"---\n{head}\n---\n\n{summary.strip()}\n", encoding="utf-8")
    return path


def main() -> None:
    ap = argparse.ArgumentParser(description="Changesets for the Trailpack addons.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add", help="add a changeset")
    a.add_argument("bump", choices=BUMPS)
    a.add_argument("addons", help="comma-separated, e.g. core or core,memo")
    a.add_argument("summary")
    sub.add_parser("status", help="show what a merge to main would release")
    p = sub.add_parser("plan", help="release plan (CI)")
    p.add_argument("--json", help="write the plan to this file")
    sh = sub.add_parser("show", help="print a plan file for the workflow (CI)")
    sh.add_argument("plan")
    sh.add_argument("format", choices=("summary", "cmake", "tsv", "count"))
    ap_apply = sub.add_parser("apply", help="write CHANGELOGs, delete consumed changesets (CI)")
    ap_apply.add_argument("plan")
    args = ap.parse_args()

    if args.cmd == "add":
        print(add(ROOT, args.bump, args.addons.split(","), args.summary).relative_to(ROOT))
    elif args.cmd == "status":
        releases = plan(ROOT)
        if not releases:
            print("No pending changesets - a merge to main releases nothing.")
        for r in releases:
            kind = "pre-release" if r["prerelease"] else "release"
            print(f"{r['name']}: {r['previous'] or '(first)'} -> {r['version']} ({r['bump']}, {kind}), "
                  f"{len(r['files'])} changeset(s)")
    elif args.cmd == "plan":
        releases = plan(ROOT)
        text = json.dumps(releases, indent=2)
        if args.json:
            Path(args.json).write_text(text, encoding="utf-8")
        else:
            print(text)
    elif args.cmd == "show":
        print(show(json.loads(Path(args.plan).read_text(encoding="utf-8")), args.format, Path(".")))
    elif args.cmd == "apply":
        apply(ROOT, json.loads(Path(args.plan).read_text(encoding="utf-8")))


if __name__ == "__main__":
    main()
