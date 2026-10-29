# Trailpack Memo — Working Plan

> Living plan: current state and open backlog. The rules (build, fork identity, code rules,
> optional dependencies) are in [`../AGENTS.md`](../AGENTS.md). Work the backlog top-down unless
> the user says otherwise, tick boxes (`- [x]`) as tasks land, and update "Current state" when the
> architecture changes.
>
> History: this file replaces the original upstream plan `2026-06-11-magpie-memo.md` (Magpie Memo
> by PieOrCake), which built the base addon and is fully implemented. Read it via
> `git show f64c3e3:plans/2026-06-11-magpie-memo.md`.

---

## Current state (as of 2026-10-29)

All upstream stages (scaffold, CRUD + persistence, view/edit + unsaved modal, markdown render,
chips with Decoder Ring, icon cache, Pie UI waypoint/open-link) plus the fork features are done.
Forked from upstream commit `aa02219`. Host tests: 80 test functions in
`tests/host_tests_main.cpp`, all green.

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
| `DecoderClient.{h,cpp}` | no | Decoder Ring consumer |
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
- Note ids are globally unique (32 hex, random 128 bit, `IsNoteId`); `FromJson` migrates legacy
  counter ids (`00000000`, Magpie Memo) and `NotesWindow::Init` saves right after loading so ids
  stay stable.
- Export/import: `NotesStore::ExportJson` / `PrepareImport` / `AddImported` (pure, host-tested;
  format `trailpack-memo-export` v1 with ids; import also takes a bare `notes.json` array). Import
  shows a selection modal; duplicates (same id, or same title + body when the file has no valid
  id) are shown locked and never added. Windows file dialogs run on a worker thread
  (`std::async`) and are polled in `Render()`.
- User colours (border / background / text) in `Theme::Colors()`, pushed for the whole window by
  `Theme::PushWindowStyle()`; background/text only override Nexus's style once picked
  (`color_*` in `settings.json`, null = Nexus default).
- Text-size setting (70-200 %, Nexus options, `text_scale` in `settings.json`) via
  `SetWindowFontScale`; `RenderMarkdown` uses `ImGui::GetFontSize()` so view text follows it.

---

## Backlog

Each task: feature branch, Definition of Done from `AGENTS.md`, then tell the user what to verify
in game.

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
- [x] Removed `build-magpie-memo-prompt.md` and `docs/superpowers/` (upstream's build prompt and
      Pie UI "open map" spec/plan; implemented, stale names/paths). Still in git history (`f64c3e3`).

### B4 — Upstream sync procedure
- [ ] Document how to pull new `PieOrCake/magpie_memo` commits into `trailpack-memo/` (e.g.
      `git subtree`-style or manual diff of `src/` against upstream at a given commit), keeping our
      identity and fork features. Record the last synced upstream commit here (currently `aa02219`).

### Ideas (not scheduled — only on user request)
- Search/filter in the note list; note reordering (drag or up/down).
- Export/import a single note as plain `.md` (JSON export/import exists).
- Pinning or a "daily routine" auto-uncheck at reset time.

---

## Changelog of this plan
- 2026-10-29 — Rewritten from the upstream implementation plan for the Trailpack Memo fork in the
  monorepo; rules moved to `AGENTS.md`; B3 done.
