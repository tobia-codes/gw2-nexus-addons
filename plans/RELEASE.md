# Release plan — Trailpack Core + Trailpack Memo in the Nexus Addon Library

> Internal plan for agents and the user (no README here). Agreed with the user on 2026-10-30
> (grilling session, decisions R1-R14) — ask before changing a decision. Tick boxes as tasks
> land. Nothing is published while we are still developing and testing locally.

## Decisions

- **R1 — Where:** the official Nexus Addon Library (Raidcore). GitHub pre-releases for testers
  before that.
- **R2 — Blockers for the first release** (everything else comes after it):
  1. in-game tests of Core stages 4 + 5 passed (with Core, without Core, Core loaded / unloaded
     while Memo runs) — test lists in `trailpack-core/plans/PLAN.md`;
  2. Memo backlog **B1** fixed (keys like WASD leak into the game while typing in a note);
  3. Core's own German item labels checked in game (`trailpack-core/src/resolve/Labels.cpp`);
  4. wiki test fixtures replaced by captured responses once the wiki answers again
     (`trailpack-core/tests/core_tests.cpp`, header comment).
  Not blockers: trading-post prices, account data, item search, breakbar, event table addon.
- **R3 — Versions:** both start public at **1.0.0**, then SemVer each on its own (patch = fix,
  minor = feature, major = break, e.g. Memo needing a newer Core ABI).
- **R4 — Policy tier:** both **Tier 0** (official API / wiki only; Memo never touches the game).
- **R5 — Pre-releases:** versions below 1.0.0 (0.9.x) are published as GitHub pre-releases
  automatically; later betas use tags `<addon>-pre-v<version>`. Nexus shows pre-releases only to
  users who allow them. No suffixes like `-beta`: Nexus ignores such tags.
- **R6 — Wording:** Core is the data service; for Memo it **unlocks extended features** (chip
  details, event timers), Memo works without it. Never "requires Core".
- **R7 — Raidcore contact before the first pre-release** (see below).
- **R8 — Release repos:** one release-only repository per addon (`tobia-codes/trailpack-core`,
  `tobia-codes/trailpack-memo`), addons use `UP_GitHub` (Core PLAN D20).
- **R9 — Release-repo text:** written in this repo (`<addon>/release/README.md` + `images/`),
  copied to the release repository by the release workflow together with `LICENSE`.
- **R10 — Screenshots:** the user takes them during the in-game tests (list below).
- **R11 — Changelog:** `<addon>/CHANGELOG.md`; every player-visible change goes under
  `## Unreleased` (or run `/changelog`, skill in `.claude/skills/changelog/`). The release script
  turns it into `## <version> - <date>`; the workflow uses that section as release notes and
  fails without it.
- **R12 — Pre-release rule:** see R5.
- **R13 — Release script:** `python3 tools/release.py <core|memo> <version> [--pre]`: clean tree,
  changelog section, `V_*` defines, commit, tag, asks before pushing. The tag starts
  `.github/workflows/release.yml`.
- **R14 — Language:** release READMEs and notes in English; they say the addons speak English
  and German.

Also in place: `.github/workflows/ci.yml` runs the host tests and then the DLL builds on every
push; rules for tests / README / changelog in `CLAUDE.md` and both `AGENTS.md`.

## Raidcore contact (R7)

**How:** Raidcore Discord, https://discord.gg/raidcore (linked on raidcore.gg/Nexus). Read the
pinned messages / channel list for an addon-submission or developer channel and ask there.

**What to ask, and why:**

1. **"We'd like Trailpack Core and Trailpack Memo listed in the Nexus Addon Library — what is the
   process?"** — Why: the library is a list Raidcore maintains (`api.raidcore.gg/addonlibrary`);
   there is no public submission form or repo, so only they can add us.
2. **"Which addon signature should we use?"** — Why: Nexus identifies an addon by its
   signature (settings, updates, library entry). Raidcore's docs say: addons hosted by Raidcore
   use the ID Raidcore assigns, others "any negative integer"; yet the GitHub-hosted addons in the
   library all have positive IDs. Ours are positive today (Core `0x5450436F`, Memo `0x54504D6D`).
   Changing it before the first release costs nothing; afterwards users lose their Nexus settings
   for the addon. So the answer must be in before the first pre-release.
3. **"Is Tier 0 right for both?"** — Why: the tier decides the warning shown in the library
   (addon policy, raidcore.gg/gw2/addon-policy); we want them to confirm it, not guess.
4. **"Can an entry say that Memo works better with Core?"** — Why: Nexus has no addon
   dependencies; the library text is the only place to point players to Core (R6).

**Submission text** (English; adjust after their answers):

> **Trailpack Core** — Game data for the Trailpack addons: names, icons and tooltips for chat
> links (items, skills, traits, recipes, skins), built-in waypoints and event timers. Official
> GW2 API and wiki only, no API key, cached and rate-limited. English / German.
> Download: https://github.com/tobia-codes/trailpack-core — Tier 0 — tags: utility, data, library
>
> **Trailpack Memo** — Notepad with markdown, checklists, groups, daily / weekly notes and
> chat-link chips. Extended features (chip details, event timers) with Trailpack Core.
> English / German. Download: https://github.com/tobia-codes/trailpack-memo — Tier 0 —
> tags: notes, utility
>
> Source of both: https://github.com/tobia-codes/gw2-nexus-addons (MIT)

## Screenshots (R10)

PNG, game UI at normal size, no personal data (character / account names) visible. Put them in
`<addon>/release/images/` and un-comment the image lines in `<addon>/release/README.md`.

| File | Shows |
|---|---|
| `trailpack-memo/release/images/notes.png` | Note window in view mode: a checklist, an item chip (rarity colour + icon), a waypoint chip, a build chip and a running / upcoming event chip |
| `trailpack-memo/release/images/tooltip.png` | Hover tooltip of an equipped item chip with rune / sigil bonuses |
| `trailpack-memo/release/images/editor.png` | Edit mode with the toolbar and the `/e` suggestion list open |
| `trailpack-core/release/images/options.png` | Nexus options > Trailpack Core, "General" open, status "Online" |

## Steps

### Before the first pre-release
- [ ] Release blockers R2 done (each with its own branch / PR).
- [ ] Raidcore contacted (R7); signatures changed if they ask for it (both `dllmain.cpp`,
      AGENTS identity tables).
- [ ] Release repositories created (public, README only) — the user runs:
      `gh repo create tobia-codes/trailpack-core --public --add-readme` and the same for
      `trailpack-memo` (Claude Code's auto mode may not create public repositories).
- [ ] Fine-grained token (github.com/settings/personal-access-tokens/new: only the two release
      repos, Repository permission "Contents: Read and write"), stored with
      `gh secret set RELEASE_TOKEN --repo tobia-codes/gw2-nexus-addons`.
- [ ] Screenshots (R10) in place.
- [ ] `/changelog` run, `## Unreleased` of both addons reviewed.

### Pre-release for testers
- [ ] `python3 tools/release.py core 0.9.0`, then `python3 tools/release.py memo 0.9.0`
      (pre-releases automatically, R5). Check the release pages, the README sync and an install
      from the pre-release in game (Nexus: allow pre-releases).

### 1.0.0 and the library
- [ ] Tester feedback handled; `python3 tools/release.py core 1.0.0`, then memo.
- [ ] Library entries requested with the submission text; check both install from the library
      and update through Nexus.
- [ ] Root `README.md`: Core no longer "(in development)"; links to the release repositories.

## Changelog of this plan
- 2026-10-30 — Plan set up with the user (R1-R14); release script, CI, changelog skill, release
  READMEs added.
