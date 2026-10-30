# Release plan — Trailpack Core + Trailpack Memo in the Nexus Addon Library

> Internal plan for agents and the user (no README here). Agreed with the user on 2026-10-30
> (grilling sessions, decisions R1-R17) — ask before changing a decision. Tick boxes as tasks
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
- **R5 — Pre-releases:** versions below 1.0.0 are published as GitHub pre-releases
  automatically. Each addon's first release is its `initialVersion` 0.9.0 (`.changeset/config.json`,
  the bump of the first changesets is ignored); a later `major` changeset ("First stable
  release") makes it 1.0.0. Nexus shows pre-releases only to users who allow them; no suffixes like
  `-beta` (Nexus ignores such tags). Betas after 1.0.0 (a Changesets "pre mode") are not built yet.
- **R6 — Wording:** Core is the data service; for Memo it **unlocks extended features** (chip
  details, event timers), Memo works without it. Never "requires Core".
- **R7 — Raidcore contact before the first pre-release** (see below).
- **R8 — Release repos:** one release-only repository per addon (`tobia-codes/trailpack-core`,
  `tobia-codes/trailpack-memo`), addons use `UP_GitHub` (Core PLAN D20).
- **R9 — Release-repo text:** written in this repo (`<addon>/release/README.md` + `images/`),
  copied to the release repository by the release workflow together with `LICENSE`.
- **R10 — Screenshots:** the user takes them during the in-game tests (list below).
- **R11 — Changesets (replaces the "## Unreleased" changelog and the release script):** we use
  the Changesets idea and file format (https://github.com/changesets/changesets) with our own
  stdlib script `tools/changeset.py` (no Node in a C++ repo; the format stays compatible, so the
  real tool could be adopted later). Every player-visible change adds `.changeset/<name>.md`
  (`"core": minor` front matter + text for players) — `python3 tools/changeset.py add ...` or the
  `/changeset` skill. Versions and CHANGELOGs are never edited by hand.
- **R12 — Versions are computed at release:** when `develop` is merged into `main`, per addon:
  last release tag `<addon>-v<version>` + the highest bump of its pending changesets (pending =
  the file did not exist yet at that tag) = new version. Addons without pending changesets are
  not released. Preview: `python3 tools/changeset.py status`.
- **R13 — Release workflow** (`.github/workflows/release.yml`, on push to `main`): plan ->
  tests -> release build (version + update channel injected) -> tag `<addon>-v<version>` here ->
  release `v<version>` (notes from the changesets) in the release repository + README / images /
  LICENSE synced -> CHANGELOG sections written and consumed changesets deleted, pushed to `develop`.
- **R14 — Language:** release READMEs and notes in English; they say the addons speak English
  and German.
- **R15 — Branches (git flow):** `develop` is the default branch; feature branches start there and
  go back by PR (rebase merge). Ruleset on `develop`: no force push, no deletion — direct pushes
  allowed, no CI required (faster development). `main` = released state; ruleset "Protect main":
  PR required, checks `test-and-build` and `source-is-develop` (`.github/workflows/pr-source.yml`:
  only PRs from `develop`), no force push / deletion, no bypass. `develop` -> `main` is merged
  with a **merge commit** (a rebase would rewrite the commits and make the next `develop` -> `main`
  PR repeat them).
- **R16 — Version in the build, not in the source:** `V_*` and the update channel come from CMake
  (`trailpack_release()` in the root CMakeLists: `-DTRAILPACK_RELEASE=ON
  -DTRAILPACK_VERSION_<Target>=x.y.z`). Local builds are 0.0.0 with `UP_None`, so Nexus never
  replaces a test DLL with a published release.
- **R17 — Safety switch:** the release workflow publishes only when the repository variable
  `RELEASES_ENABLED` is `true`; until then a merge to `main` only shows the plan in the run summary.

Also in place: `.github/workflows/ci.yml` runs the tool tests, checks the changesets, then the
host tests and the DLL builds on every push; rules in `CLAUDE.md` and both `AGENTS.md`.

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
`<addon>/release/images/` and add them to `<addon>/release/README.md` (and the addon README).

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
- [ ] `/changeset` run; `python3 tools/changeset.py status` shows Core 0.9.0 and Memo 0.9.0
      (`.changeset/initial-core.md`, `initial-memo.md` hold the first release notes).
- [ ] Repository variable `RELEASES_ENABLED` = `true` (Settings > Secrets and variables >
      Actions > Variables), only now.

### Pre-release for testers
- [ ] PR `develop` -> `main`, merge commit once CI is green: the workflow releases Core 0.9.0
      and Memo 0.9.0 as pre-releases (R5). Check the run summary, the release pages, the README
      sync, the CHANGELOG commit on `develop` and an install from the pre-release in game (Nexus:
      allow pre-releases). Check the release pages, the README sync and an install
      from the pre-release in game (Nexus: allow pre-releases).

### 1.0.0 and the library
- [ ] Tester feedback handled (each fix with a changeset); a `major` changeset "First stable
      release" for both, then `develop` -> `main`: both become 1.0.0.
- [ ] Library entries requested with the submission text; check both install from the library
      and update through Nexus.
- [ ] Root `README.md`: Core no longer "(in development)"; links to the release repositories.

## Changelog of this plan
- 2026-10-30 — Plan set up with the user (R1-R14); release script, CI, changelog skill, release
  READMEs added.
- 2026-10-30 — `main` protected (ruleset); release script goes through a release PR (R13).
- 2026-10-30 — Git flow with `develop` (R15), changesets instead of "## Unreleased" + release
  script (R11-R13), version from the build (R16), safety switch (R17).
