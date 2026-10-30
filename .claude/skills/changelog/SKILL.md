---
name: changelog
description: Bring the addons' CHANGELOG.md files up to date with the changes since their last release (entries under "## Unreleased"). Use when the user runs /changelog or asks to update / maintain the changelog.
---

# Update the changelogs

Every addon folder (`trailpack-core/`, `trailpack-memo/`, later ones too) has a `CHANGELOG.md`
whose `## Unreleased` section collects the player-visible changes until the next release.
`tools/release.py` later renames that section to the released version (plans/RELEASE.md).

## Steps

1. For each addon folder with a `CHANGELOG.md`:
   - Find its last release tag: `git tag -l '<addon>-v*' '<addon>-pre-v*' --sort=-creatordate | head -1`
     (`core` / `memo`). No tag yet: everything counts as unreleased.
   - Collect what changed since then: `git log --oneline <tag>..HEAD -- <folder> libs/`, plus
     uncommitted work (`git status`, `git diff`) in that folder and in the `libs/` it links.
   - Read the existing `## Unreleased` entries so nothing is added twice.
2. Write **player-visible** changes only, in English, as short bullets under `## Unreleased`,
   grouped as `### Added`, `### Changed`, `### Fixed`, `### Removed` (only the groups that have
   entries). Name what the player notices ("Event chips show ..."), not code ("refactor X").
   Internal work (tests, refactors, CI, docs for agents) is left out. A change in `libs/` goes
   into the changelog of every addon whose behaviour it changes.
3. Never edit released sections (`## <version> - <date>`), never invent a version number — the
   release script sets it.
4. Show the user the new entries per addon (in German, as a short list) and ask before
   committing; commit only when they say so (repo rules in CLAUDE.md).
