# Trailpack Memo — Working Plan

> **For Claude:** Read this file before working on `trailpack-memo/`. It is the living plan: current
> state, invariants, and the open backlog. Work the backlog top-down unless the user says otherwise,
> tick boxes (`- [x]`) as tasks land, and update "Current state" when the architecture changes.
> The user writes German — reply in German; code, comments and commits stay English.
>
> History: this file replaces the original upstream plan `2026-06-11-magpie-memo.md` (Magpie Memo
> by PieOrCake), which built the base addon. That plan is fully implemented; its decisions are kept
> below where they still bind. Read it via `git show f64c3e3:plans/2026-06-11-magpie-memo.md`.

---

## What this is

A Raidcore Nexus addon for Guild Wars 2: a notepad with multiple markdown notes, task-list
checkboxes, a formatting toolbar, live preview, and inline GW2 chat-link "chips" (icon + name +
tooltip). It is a **standalone fork** of [PieOrCake/magpie_memo](https://github.com/PieOrCake/magpie_memo)
(forked from upstream commit `aa02219`) and runs **side by side** with Magpie Memo.

It lives in the monorepo `gw2-nexus-addons` (public: `tobia-codes/gw2-nexus-addons`), folder
`trailpack-memo/`. Shared third-party code (ImGui 1.80, nlohmann/json, `nexus/Nexus.h`) is in
`../shared/`.

## Build & test (run from the repo root)

```
cmake -S . -B build && cmake --build build --target TrailpackMemo           # -> build/TrailpackMemo.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                       # pure-logic host tests
```

- Toolchain (`x86_64-w64-mingw32-g++`, `cmake`, `g++`) is installed on the WSL box. If one is ever
  missing: Claude has no sudo — ask the user to run `! sudo apt install mingw-w64 cmake g++ make`.
- **Definition of Done for any change:** DLL builds clean **and** host tests green. In-game behaviour
  can only be verified by the user — say what to test, never claim it works in game.

---

## Invariants (do not break)

**Fork identity** — must stay distinct from Magpie Memo so both load together:

| Thing | Value | Where |
|---|---|---|
| Name / window | `Trailpack Memo` | `src/dllmain.cpp` (`WINDOW_NAME`) |
| Signature | `0x54504D6D` ("TPMm"; Magpie = `0x4D61704D`) | `src/dllmain.cpp` |
| DLL | `TrailpackMemo.dll`, export via `TrailpackMemo.def` | `CMakeLists.txt` |
| Data dir | `addons/TrailpackMemo/` (`notes.json`, `settings.json`) | `Paths_GetAddonDirectory("TrailpackMemo")` |
| Toggle keybind | `CTRL+SHIFT+T` (Magpie: `CTRL+SHIFT+M`) | `src/dllmain.cpp` |
| Texture ids | prefix `TPM_ICON_` | `src/IconCache.cpp` |
| Author | `PieOrCake.7635 & tobia-codes` | `src/dllmain.cpp` |
| Updates | `UP_None`, `UpdateLink = nullptr` (see backlog B2) | `src/dllmain.cpp` |

**Code rules**

- Internal C++ namespace stays **`Magpie`** (deliberately, to ease upstream merges). New
  identifiers that the game/Nexus can see (signature, ids, events, textures, paths) must **not**
  collide with Magpie Memo's.
- **Vendored codec is byte-identical:** `src/chat/ChatLinks.{h,cpp}`, `src/chat/SpecData.h` come
  from Pie UI. Never edit them; a codec bug is fixed upstream and re-vendored.
- **Pure units stay pure** (no ImGui/Nexus/Win32 includes) so the host-test target builds:
  `NotesStore`, `Markdown`, `MarkdownEdit`, `ChipCodec`, `ChipResolver`, `ChipColor`, `IconUrl`,
  `chat/ChatLinks`. New logic goes into a pure unit + host test wherever possible.
- **All rendered text is ASCII** (Nexus font) — no em-dashes, arrows, Unicode bullets in UI strings.
- **No game-memory reading, no game-function hooks.** Anything that seems to need it is out of scope.
- **Optional deps never crash:** Decoder Ring and Pie UI may be absent or (un)load mid-session.
  - Decoder Ring: never cache `DecoderRingApi*`; `GetDecoder()` (DataLink `DECODER_RING_API` +
    `apiVersion == 1`) before every call; presence is live, not latched; on `UNLOADING` drop the
    record cache and pending keys; `RESOLVED` payload is valid only during the handler — copy out;
    check `DecoderRecord.schemaVersion`. Build links (`0x0D`) require the full chat code string.
  - Pie UI: presence latched on `EV_PIE_UI_READY`; `OpenChatLink` raises `EV_PIEUI_OPEN_CHATLINK`
    with the `[&...]` string (fire-and-forget, no-op if absent).
- **Never deploy the DLL** into the game folder and **never bump the `V_*` version** unless asked.
- Match surrounding style; keep `README.md` (addon + root table) in sync with user-visible changes.
- Git: commits on a feature branch when the user asks to commit; `CLAUDE.md`/`handover.md` are
  gitignored LLM files.

---

## Current state (as of 2026-10-29)

All upstream stages (scaffold, CRUD + persistence, view/edit + unsaved modal, markdown render,
chips with Decoder Ring, icon cache, Pie UI waypoint/open-link) plus the fork features are done.
Host tests: 80 test functions in `tests/host_tests_main.cpp`, all green.

### Module map (`src/`)

| Module | Pure? | Role |
|---|---|---|
| `dllmain.cpp` | no | Nexus lifecycle, identity, keybind, quick access, options pane, `settings.json` (preview toggle, QA icon) |
| `NotesWindow.{h,cpp}` | no | Two-pane UI, view/edit modes, Save/Cancel, unsaved + delete modals, edit toolbar, preview, task clicks, `notes.json` I/O |
| `NotesStore.{h,cpp}` | yes | Note model `{id,title,body}`, CRUD, JSON round-trip |
| `Markdown.{h,cpp}` | yes | Line parser (Paragraph/Heading/Bullet/Task; Text/Bold/Italic/Chip spans); task count/toggle/uncheck-all |
| `MarkdownEdit.{h,cpp}` | yes | Toolbar commands on a cell model: wrap bold/italic, toggle line prefixes, list continuation on Enter |
| `MarkdownRender.{h,cpp}` | no | ImGui view rendering: FontBig headings, faux bold (double draw), faux italic (shear), checkboxes, rich chips |
| `ChipTextEdit.{h,cpp}` | no | Multi-line stb_textedit editor with atomic chip cells (adapted from Pie UI), `ApplyEdit` for toolbar ops |
| `ChipCodec.{h,cpp}` | yes | Text <-> cell round-trip (chat codes -> one chip cell), UTF-8 helpers |
| `ChipResolver.{h,cpp}` | yes | Offline structural label + colour from the codec (`[Item]`, `[Mirage Build]`) |
| `ChipColor.{h,cpp}` | yes | GW2 link colours (per type, per item rarity) |
| `ChipRich.{h,cpp}` | no | Rich chip: resolved name/icon/tooltip, right-click Copy code / Open wiki, Pie UI left-click |
| `DecoderClient.{h,cpp}` | no | Decoder Ring consumer (contract above) |
| `PieUiClient.{h,cpp}` | no | Pie UI consumer (`OpenChatLink`) |
| `IconCache.{h,cpp}` / `IconUrl.{h,cpp}` | no / yes | URL -> texture via Nexus `Textures_*FromURL`, failure set (retryable); FNV-1a key, URL split |
| `Theme.{h,cpp}` | no | Accent palette (gold border, green/amber buttons, edit tint) |
| `chat/*` | yes | **Vendored** Pie UI codec — do not edit |

### Fork features (beyond upstream)

- GFM task lists `- [ ]` / `- [x]`: clickable in view mode (saves immediately), checked items dimmed
  and struck through, `done/total` progress in the note list, context menu "Uncheck all tasks".
- Edit toolbar B / I / H1 / H2 / list / task + `Ctrl+B` / `Ctrl+I`, toggles off when reapplied.
- Enter continues bullet/task lists; Enter on an empty item ends the list.
- Optional side-by-side live preview, persisted in `settings.json`.

---

## Backlog

Each task: do it on a feature branch, keep the DoD (DLL + host tests), then report to the user what
to verify in game.

### B1 — WndProc key capture while editing
**Why:** `dllmain.cpp` has a TODO: while a chip editor is focused, held keys can leak to the game
(e.g. WASD moves, `H` opens Hero panel) because `WantTextInput` flickers.
**Files:** `src/dllmain.cpp` (uses `Magpie::ChipInputActive()` / `Magpie::RequestChipBlur()` from
`ChipTextEdit.h`).
- [ ] Register a WndProc via `APIDefs->WndProc_Register` in `AddonLoad`, deregister in `AddonUnload`.
- [ ] Keyboard messages (`WM_KEYDOWN/UP`, `WM_CHAR`, `WM_SYSKEY*`) while `ChipInputActive()`: reinforce
      ImGui capture so they do not reach the game (check Nexus WndProc return semantics in
      `shared/include/nexus/Nexus.h` — consume vs. pass through).
- [ ] Left click with no ImGui window hovered -> `RequestChipBlur()`.
- [ ] Remove the TODO comment. User test: type `wasd h` in a note — character must not move, Hero
      panel must not open; clicking into the world unfocuses the editor.

### B2 — Update channel for a monorepo (needs a user decision first)
**Why:** `Provider = UP_None` — users never get updates. Nexus' GitHub provider reads the latest
release of one repo, but the monorepo will hold several addon DLLs.
- [ ] Ask the user which option they want (do not decide alone): (a) one release per addon with
      tag prefixes + check how Nexus picks the asset, (b) a separate release-only repo per addon,
      (c) `UP_Direct` with a raw URL to a versioned DLL.
- [ ] Implement the chosen option (`Provider`, `UpdateLink`), plus a release step (e.g. GitHub
      Actions building with MinGW and attaching `TrailpackMemo.dll`). Only bump `V_*` when asked.

### B3 — Clean up outdated upstream docs
- [ ] `docs/superpowers/` holds upstream's Pie UI "open map" spec + plan (unticked boxes, API
      renamed since: `OpenMap` -> `OpenChatLink`, event `EV_PIEUI_OPEN_CHATLINK`). It is implemented.
      Ask the user whether to delete it or move it to `docs/upstream/` with a "historical" note.

### B4 — Upstream sync procedure
- [ ] Add `upstream` remote handling notes: how to pull new `PieOrCake/magpie_memo` commits into
      `trailpack-memo/` (e.g. `git subtree`-style or manual diff of `src/` against upstream at a
      given commit), keeping our identity table and fork features. Record the last synced upstream
      commit here (currently `aa02219`).

### Ideas (not scheduled — only on user request)
- Search/filter in the note list; note reordering (drag or up/down).
- Export/import a single note as `.md`.
- Pinning or a "daily routine" auto-uncheck at reset time.

---

## Changelog of this plan
- 2026-10-29 — Rewritten from the upstream implementation plan for the Trailpack Memo fork in the
  monorepo: current state, invariants, backlog B1–B4.
