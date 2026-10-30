# gw2-nexus-addons

Monorepo of Raidcore Nexus addons for Guild Wars 2 (public: `tobia-codes/gw2-nexus-addons`).
Layout, build commands and "Adding an addon" are in [README.md](README.md).

- The user writes German — reply in German. Code, comments, commit messages and docs stay English.
- One folder per addon; each has its own `AGENTS.md` (rules) and `plans/PLAN.md` (state + backlog),
  loaded via that folder's `CLAUDE.md`. Shared third-party code lives in `shared/` — don't modify it.
- Commit/push only when asked, on a feature branch (not `main`).

## Addons

- `trailpack-memo/` — Trailpack Memo (notepad with markdown, checklists, chat-link chips)
- `trailpack-codex/` — Trailpack Codex (planned: headless data service required by all Trailpack
  addons; chat-link resolution + built-in events / waypoints)
