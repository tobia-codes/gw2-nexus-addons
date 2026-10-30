# Trailpack Core — Agent Instructions

Rules for AI coding agents working in `trailpack-core/`. Decisions, stages and open tasks live in
[`plans/PLAN.md`](plans/PLAN.md) — read it before starting a task and update it when one lands.

## What this is

A headless Raidcore Nexus addon for Guild Wars 2 and the data service of every Trailpack
addon: it resolves chat links into display records (API data, disk-cached) and serves built-in
game data (events, waypoints, specialisations). Shared third-party code (ImGui 1.80,
nlohmann/json, `nexus/Nexus.h`) is in `../shared/`, own shared libraries in `../libs/`.

## Build & test (run from the repo root)

```
cmake -S . -B build && cmake --build build --target TrailpackCore          # -> build/TrailpackCore.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                       # pure-logic host tests
```

- **Definition of Done:** host tests (incl. new ones for the change) green, **then** the DLL
  builds clean; `README.md` (and `release/README.md` if the player-facing text changes) updated
  and a changeset in `../.changeset/` (`/changeset`) for player-visible changes. In-game behaviour can only be
  verified by the user — say what to test, never claim it works in game.

## Identity

| Thing | Value |
|---|---|
| Name | `Trailpack Core` |
| Signature | `0x5450436F` ("TPCo") |
| DLL | `TrailpackCore.dll`, export via `TrailpackCore.def` |
| Data dir | `addons/TrailpackCore/` (`settings.json`, `cache/<lang>/`) |
| DataLink / events | `TRAILPACK_CORE_API`, `EV_TRAILPACK_CORE_{READY,UNLOADING,PING,RESOLVED}` |
| Author | `tobia-codes` |
| Updates | Release builds only: `UP_GitHub`, release repo `tobia-codes/trailpack-core`; version from the changesets (`TRAILPACK_VERSION_TrailpackCore`); local builds 0.0.0, `UP_None` |

## Code rules

- C++ namespace: **`Trailpack::Core`**. Source layout like Memo: feature folders under `src/`
  (module map in PLAN), includes relative to `src/`; `src/generated/` is written by
  `tools/gen_events.py` / `tools/gen_gamedata.py` — never edit by hand, re-run the generator.
- Shared libraries: `libs/trailpack-chatlinks` (codec), `libs/trailpack-files` (`SafeFile`),
  `libs/trailpack-ui` (settings-page look). Change them there, with a test.
- `dllmain.cpp` only binds Nexus; behaviour goes into the pure units (`api/Service`,
  `resolve/*`, `events/*`) and gets a host test in `tests/core_tests.cpp`. API fixtures in
  `tests/fixtures/` are captured responses — re-capture instead of editing them.
- **ABI (`public/TrailpackCoreApi.h`):** pure (`<cstdint>` only), fixed-size PODs with
  `structSize` first, fields (and table functions) only appended, never reused for another
  meaning. No pointers into Core memory in any answer — copy into caller buffers. Any change
  updates the pinned offsets in `test_abi_layout`; bump `TRAILPACK_CORE_API_VERSION` when a
  consumer can rely on something new.
- **Pure units stay pure** (no ImGui/Nexus/Win32): parsers, cache model, batch planner, token
  bucket, event schedule / search, generated data. New logic goes into a pure unit + host test.
- **API etiquette is mandatory** (PLAN D10): every request goes through the token bucket
  (<= 2/s, burst 20), batch with `?ids=`, back off on 429 / 5xx, honest User-Agent. Account
  endpoints only with a user-entered API key (backlog, not built yet). Never add a request path
  that bypasses the queue.
- **API key:** stored only locally (DPAPI-encrypted), sent only to `api.guildwars2.com` as an
  `Authorization` header, used only for account data. Never in URLs, logs, cache files, exports,
  the ABI or any other host.
- **Never touch the game:** no memory reading, no hooks, no input.
- Languages: German and English only; UI text through the same `Tr` pattern as Memo.
- **No `<fstream>`/`<sstream>`/`<iostream>`/`<filesystem>`** — files go through `SafeFile`.
- Rendered characters: ASCII plus Latin-1 letters.

## Workflow

- **Never deploy the DLL** into the game folder — the user does that.
- Never set a version by hand: it comes from the changesets at release (plans/RELEASE.md).
- Commit/push only when asked, on a feature branch (not `main`).
