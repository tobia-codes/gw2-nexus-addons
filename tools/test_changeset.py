#!/usr/bin/env python3
"""Tests for tools/changeset.py (run: python3 -m unittest discover -s tools -p 'test_*.py')."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changeset as cs  # noqa: E402

CONFIG = {"addons": {
    "core": {"folder": "trailpack-core", "target": "TrailpackCore", "name": "Trailpack Core",
             "releaseRepo": "x/core", "initialVersion": "0.9.0"},
    "memo": {"folder": "trailpack-memo", "target": "TrailpackMemo", "name": "Trailpack Memo",
             "releaseRepo": "x/memo", "initialVersion": "0.9.0"},
}}


class Repo:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.git("init", "-q")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "t")
        (self.root / ".changeset").mkdir()
        (self.root / ".changeset" / "config.json").write_text(json.dumps(CONFIG))
        for d in ("trailpack-core", "trailpack-memo"):
            (self.root / d).mkdir()
            (self.root / d / "CHANGELOG.md").write_text("# Changelog\n\nIntro.\n")
        self.commit()

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def commit(self, msg="c"):
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", msg)

    def changeset(self, name, head, text="Something."):
        (self.root / ".changeset" / name).write_text(f"---\n{head}\n---\n\n{text}\n")


class ChangesetTests(unittest.TestCase):
    def test_bump(self):
        self.assertEqual(cs.bump("1.2.3", "patch"), "1.2.4")
        self.assertEqual(cs.bump("1.2.3", "minor"), "1.3.0")
        self.assertEqual(cs.bump("1.2.3", "major"), "2.0.0")
        self.assertEqual(cs.bump("0.9.0", "major"), "1.0.0")

    def test_parse(self):
        bumps, text = cs.parse_changeset('---\n"core": minor\nmemo: patch\n---\n\nHello\nworld\n')
        self.assertEqual(bumps, {"core": "minor", "memo": "patch"})
        self.assertEqual(text, "Hello\nworld")
        for bad in ("no front matter", '---\n"core": huge\n---\n\nx', '---\n"core": minor\n---\n\n', "---\n---\n\nx"):
            with self.assertRaises(ValueError):
                cs.parse_changeset(bad)

    def test_first_release_uses_initial_version(self):
        r = Repo()
        r.changeset("a.md", '"core": minor')
        r.commit()
        plan = cs.plan(r.root)
        self.assertEqual([(p["addon"], p["version"], p["prerelease"]) for p in plan], [("core", "0.9.0", True)])

    def test_highest_bump_since_last_tag_and_released_files_skipped(self):
        r = Repo()
        r.changeset("old.md", '"core": major', "Old")
        r.commit()
        r.git("tag", "core-v1.0.0")
        r.changeset("fix.md", '"core": patch', "Fix")
        r.changeset("feat.md", '"core": minor\n"memo": patch', "Feature")
        r.commit()
        plan = {p["addon"]: p for p in cs.plan(r.root)}
        self.assertEqual(plan["core"]["version"], "1.1.0")              # minor wins over patch
        self.assertEqual(plan["core"]["previous"], "1.0.0")
        self.assertFalse(plan["core"]["prerelease"])
        self.assertNotIn(".changeset/old.md", plan["core"]["files"])   # existed at core-v1.0.0
        self.assertIn("### Minor changes\n\n- Feature", plan["core"]["notes"])
        self.assertIn("### Patch changes\n\n- Fix", plan["core"]["notes"])
        self.assertEqual(plan["memo"]["version"], "0.9.0")              # memo: first release

    def test_nothing_pending(self):
        r = Repo()
        r.changeset("a.md", '"core": patch')
        r.commit()
        r.git("tag", "core-v0.9.0")
        self.assertEqual(cs.plan(r.root), [])

    def test_unknown_addon_fails(self):
        r = Repo()
        r.changeset("a.md", '"nope": patch')
        with self.assertRaises(SystemExit):
            cs.plan(r.root)

    def test_apply_writes_changelog_and_deletes_consumed(self):
        r = Repo()
        r.changeset("both.md", '"core": minor\n"memo": patch', "Both")
        r.changeset("core.md", '"core": patch', "Core only")
        r.commit()
        plan = [p for p in cs.plan(r.root) if p["addon"] == "core"]   # only core released
        cs.apply(r.root, plan, date="2026-10-30")
        log = (r.root / "trailpack-core" / "CHANGELOG.md").read_text()
        self.assertIn("## 0.9.0 - 2026-10-30", log)
        self.assertLess(log.index("Intro."), log.index("## 0.9.0"))
        self.assertFalse((r.root / ".changeset" / "core.md").exists())
        self.assertTrue((r.root / ".changeset" / "both.md").exists())  # memo has not consumed it
        cs.apply(r.root, plan, date="2026-10-30")                       # idempotent
        self.assertEqual((r.root / "trailpack-core" / "CHANGELOG.md").read_text().count("## 0.9.0"), 1)

    def test_show(self):
        r = Repo()
        r.changeset("a.md", '"core": minor\n"memo": patch', "Text")
        r.commit()
        plan = cs.plan(r.root)
        self.assertEqual(cs.show(plan, "count"), "2")
        self.assertEqual(cs.show(plan, "cmake"),
                         "-DTRAILPACK_VERSION_TrailpackCore=0.9.0 -DTRAILPACK_VERSION_TrailpackMemo=0.9.0")
        self.assertIn("- Trailpack Core: (first) -> 0.9.0 (minor, pre-release)", cs.show(plan, "summary"))
        self.assertEqual(cs.show([], "summary"), "Nothing to release.")
        rows = cs.show(plan, "tsv", r.root).splitlines()
        self.assertEqual(rows[0].split("\t"),
                         ["core-v0.9.0", "0.9.0", "TrailpackCore", "x/core", "Trailpack Core", "trailpack-core", "true"])
        self.assertIn("- Text", (r.root / "core-v0.9.0.md").read_text())

    def test_add(self):
        r = Repo()
        p = cs.add(r.root, "minor", ["core", "memo"], "New thing!")
        bumps, text = cs.parse_changeset(p.read_text())
        self.assertEqual(bumps, {"core": "minor", "memo": "minor"})
        self.assertEqual(text, "New thing!")
        self.assertNotEqual(p, cs.add(r.root, "minor", ["core"], "New thing!"))
        with self.assertRaises(SystemExit):
            cs.add(r.root, "minor", ["nope"], "x")


if __name__ == "__main__":
    unittest.main()
