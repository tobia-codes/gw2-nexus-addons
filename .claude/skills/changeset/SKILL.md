---
name: changeset
description: Add changesets (.changeset/*.md) for the player-visible changes that have none yet, so the next merge of develop into main releases the right addons with the right version bump. Use when the user runs /changeset or asks to add / update changesets or the changelog.
---

# Add changesets

Every player-visible change needs a changeset in `.changeset/` (Changesets format, see
`.changeset/README.md`). When `develop` is merged into `main`, the release workflow releases each
addon with pending changesets: new version = last release tag + highest bump (plans/RELEASE.md).
`CHANGELOG.md` files are written from the changesets by the workflow — never edit them by hand.

## Steps

1. See what is pending: `python3 tools/changeset.py status` and the existing files in `.changeset/`.
2. Find the changes since the addons' last release tags (`git tag -l 'core-v*' 'memo-v*'`;
   none yet: everything): `git log --oneline <tag>..HEAD -- <folder> libs/` plus uncommitted
   work (`git status`, `git diff`). A change in `libs/` counts for every addon whose behaviour it
   changes.
3. For each player-visible change without a changeset, pick the bump per addon:
   - **major**: breaks something for players or for other addons (e.g. the Core ABI needs a
     newer consumer, saved data no longer loads in an older version);
   - **minor**: a new feature or option;
   - **patch**: a fix or a small wording / look change.
   Internal work (tests, refactors, CI, agent docs) gets no changeset.
4. Write it with `python3 tools/changeset.py add <bump> <addon>[,<addon>] "<text>"`: one or two
   English sentences for players ("Event chips show ..."), not code. Several changes may share
   one file if they belong together.
5. Show the user (in German) which changesets you added — addon, bump, text — and the result of
   `python3 tools/changeset.py status`. Commit only when they say so (CLAUDE.md).
