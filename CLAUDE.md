# gw2-nexus-addons

Monorepo of Raidcore Nexus addons for Guild Wars 2 (public: `tobia-codes/gw2-nexus-addons`).
Layout, build commands and "Adding an addon" are in [README.md](README.md); the release plan
(Nexus Addon Library) is [plans/RELEASE.md](plans/RELEASE.md).

- The user writes German — reply in German. Code, comments, commit messages and docs stay English.
- One folder per addon; each has its own `AGENTS.md` (rules) and `plans/PLAN.md` (state + backlog),
  loaded via that folder's `CLAUDE.md`. Shared third-party code lives in `shared/` — don't modify it.
- Commit/push only when asked, on a feature branch. `main` is protected on GitHub (ruleset
  "Protect main"): changes only via PR, CI `test-and-build` must pass before the merge.
- **Tests always:** new or changed logic gets a host test; run the host tests before building
  (`ctest` in `build-host`, see README). CI (`.github/workflows/ci.yml`) runs them before the
  DLL build on every push — keep it green.
- **README always current:** a user-visible change updates the addon's `README.md` in the same
  change, its `release/README.md` (text of the release repository / Nexus library) when that
  text is affected, and the root README table. Docs are English.
- **Changelog:** a player-visible change gets an entry under `## Unreleased` in the addon's
  `CHANGELOG.md` (or run `/changelog`). Releases only via `tools/release.py` (plans/RELEASE.md).

## Guild Wars 2 rules (every addon, every plan — the user must never risk a ban)

Every new addon, feature and plan must stay within ArenaNet's rules. Check this **while
planning**, before any code: a plan states which game data it uses and why that is allowed. If
something is unclear or needs any of the "never" points, stop and ask the user — don't build it.

- **No advantage over other players** and no automation of gameplay (ArenaNet's third-party
  policy: https://help.guildwars2.com/hc/en-us/articles/360013625034).
- **Never:** read or write game memory, hook game functions, inject into or patch the client,
  read or alter network traffic, send input to the game (keys, clicks, movement), bot or macro
  beyond ArenaNet's macro policy (one action per key press).
- **Allowed data sources:** the official GW2 API (https://api.guildwars2.com), the official GW2
  wiki, the MumbleLink data the game publishes for tools, what the player types or pastes
  (e.g. chat links), and the Nexus addon API (UI, keybinds for our own windows, textures,
  events between addons).
- **API etiquette:** stay far below the rate limit (300 burst, 5/s per IP, shared with every tool
  on the network): batch with `?ids=`, cache, back off on 429 / 5xx, honest User-Agent
  (https://wiki.guildwars2.com/wiki/API:Best_practices). An API key only when the user enters it,
  stored locally and sent only to `api.guildwars2.com` (see `trailpack-core/AGENTS.md`).
- **Target Raidcore addon policy Tier 0** (official APIs only, https://raidcore.gg/gw2/addon-policy);
  a plan that would need a higher tier needs the user's explicit decision first.

## Addons

- `trailpack-memo/` — Trailpack Memo (notepad with markdown, checklists, chat-link chips)
- `trailpack-core/` — Trailpack Core (headless data service of the Trailpack addons; unlocks their
  extended features: chat-link resolution + built-in events / waypoints)
- `libs/` — own shared libraries (`trailpack-chatlinks`, `trailpack-files`, `trailpack-ui`)
