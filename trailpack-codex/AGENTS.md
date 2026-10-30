# Trailpack Codex — Agent Instructions

Rules for AI coding agents working in `trailpack-codex/`. Decisions, stages and open tasks live in
[`plans/PLAN.md`](plans/PLAN.md) — read it before starting a task and update it when one lands.

## What this is

A headless Raidcore Nexus addon for Guild Wars 2 and the required data service of every Trailpack
addon: it resolves chat links into display records (API data, disk-cached) and serves built-in
game data (events, waypoints, specialisations). Shared third-party code (ImGui 1.80,
nlohmann/json, `nexus/Nexus.h`) is in `../shared/`, own shared libraries in `../libs/`.

## Build & test (run from the repo root)

```
cmake -S . -B build && cmake --build build --target TrailpackCodex          # -> build/TrailpackCodex.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                       # pure-logic host tests
```

- **Definition of Done:** DLL builds clean **and** host tests green. In-game behaviour can only be
  verified by the user — say what to test, never claim it works in game.

## Identity

| Thing | Value |
|---|---|
| Name | `Trailpack Codex` |
| Signature | `0x54504378` ("TPCx") |
| DLL | `TrailpackCodex.dll`, export via `TrailpackCodex.def` |
| Data dir | `addons/TrailpackCodex/` (`settings.json`, `cache/<lang>/`) |
| DataLink / events | `TRAILPACK_CODEX_API`, `EV_TRAILPACK_CODEX_{READY,UNLOADING,PING,RESOLVED}` |
| Author | `tobia-codes` |

## Code rules

- C++ namespace: **`Trailpack::Codex`**. Source layout like Memo: feature folders under `src/`,
  includes relative to `src/`, `src/generated/` is written by `tools/*.py` — never edit by hand.
- **ABI (`public/TrailpackCodexApi.h`):** pure (`<cstdint>` only), fixed-size PODs with
  `structSize` first, fields only appended, never reused for another meaning. No pointers into
  Codex memory in any answer — copy into caller buffers. Any change gets a layout test.
- **Pure units stay pure** (no ImGui/Nexus/Win32): parsers, cache model, batch planner, token
  bucket, event schedule / search, generated data. New logic goes into a pure unit + host test.
- **API etiquette is mandatory** (PLAN D10): every request goes through the token bucket
  (<= 2/s, burst 20), batch with `?ids=`, back off on 429 / 5xx, honest User-Agent. Account
  endpoints only with a user-entered API key (backlog, not built yet). Never add a request path
  that bypasses the queue.
- **Never touch the game:** no memory reading, no hooks, no input.
- Languages: German and English only; UI text through the same `Tr` pattern as Memo.
- **No `<fstream>`/`<sstream>`/`<iostream>`/`<filesystem>`** — files go through `SafeFile`.
- Rendered characters: ASCII plus Latin-1 letters.

## Workflow

- **Never deploy the DLL** into the game folder — the user does that.
- Never bump the version defines unless asked.
- Commit/push only when asked, on a feature branch (not `main`).
