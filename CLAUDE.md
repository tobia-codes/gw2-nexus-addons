# gw2-nexus-addons

Monorepo of Raidcore Nexus addons for Guild Wars 2 (public: `tobia-codes/gw2-nexus-addons`).
Layout, build commands and "Adding an addon" are in [README.md](README.md); the release plan
(Nexus Addon Library) is [plans/RELEASE.md](plans/RELEASE.md).

- The user writes German — reply in German. Code, comments, commit messages and docs stay English.
- One folder per addon; each has its own `AGENTS.md` (rules) and `plans/PLAN.md` (state + backlog),
  loaded via that folder's `CLAUDE.md`. Shared third-party code lives in `shared/` — don't modify it.
- Commit/push only when asked, on a feature branch (not `main`).
- **Tests always:** new or changed logic gets a host test; run the host tests before building
  (`ctest` in `build-host`, see README). CI (`.github/workflows/ci.yml`) runs them before the
  DLL build on every push — keep it green.
- **README always current:** a user-visible change updates the addon's `README.md` in the same
  change, its `release/README.md` (text of the release repository / Nexus library) when that
  text is affected, and the root README table. Docs are English.
- **Changelog:** a player-visible change gets an entry under `## Unreleased` in the addon's
  `CHANGELOG.md` (or run `/changelog`). Releases only via `tools/release.py` (plans/RELEASE.md).

## Addons

- `trailpack-memo/` — Trailpack Memo (notepad with markdown, checklists, chat-link chips)
- `trailpack-core/` — Trailpack Core (headless data service of the Trailpack addons; unlocks their
  extended features: chat-link resolution + built-in events / waypoints)
- `libs/` — own shared libraries (`trailpack-chatlinks`, `trailpack-files`, `trailpack-ui`)
