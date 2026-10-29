# Trailpack Memo — Working Plan

> Living plan: current state and open backlog. The rules (build, identity, code rules,
> optional dependencies) are in [`../AGENTS.md`](../AGENTS.md). Work the backlog top-down unless
> the user says otherwise, tick boxes (`- [x]`) as tasks land, and update "Current state" when the
> architecture changes.

---

## Current state (as of 2026-10-29)

Base (CRUD + persistence, view/edit + unsaved modal, markdown render, chips with Decoder Ring,
icon cache, Pie UI waypoint/open-link) and the features below are done. Standalone addon, C++
namespace `Trailpack`. Host tests: 80 test functions in
`tests/host_tests_main.cpp`, all green.

### Module map (`src/`)

| Module | Pure? | Role |
|---|---|---|
| `dllmain.cpp` | no | Nexus lifecycle, identity, keybind, quick access, options pane; maps `Settings` <-> live state |
| `Settings.{h,cpp}` | yes | settings.json model with validation/clamping (`g_Settings`); `ColorSet` (all colour options) + built-in Light/Dark, user themes |
| `Events.{h,cpp}` | yes | Event timers (B5): lookup, UTC schedule maths, search, chip time text |
| `events/EventData.{h,cpp}` | yes | **Generated** event table (`tools/gen_events.py`) — never edit by hand |
| `Backup.{h,cpp}` | yes | Backup file format (`trailpack-memo-backup` v1: notes + settings) |
| `FileDialog.{h,cpp}` | no | Windows open/save dialog (worker thread), JSON filter; used by export/import and backups |
| `SafeFile.{h,cpp}` | yes | Atomic write (+ `.bak`), read, set aside unreadable files |
| `NotesWindow.{h,cpp}` | no | Note state, load/save, navigation, frame layout, popup requests (`requestPopup_` / `beginModal_`), caches |
| `NotesList.cpp` | no | Left column: groups, note rows, menu button, splitter |
| `NotesDetail.cpp` | no | Right pane: view / edit modes, toolbar, rendered body |
| `NotesModals.cpp` | no | Confirmations, group name / colour / delete, bulk delete, notice |
| `NotesImportExport.cpp` | no | File dialogs (worker thread), export, import selection |
| `HelpWindow.{h,cpp}` | no | "?" title-bar button + guide window (sections translated via `Tr`; update it with new features) |
| `NotesUi.{h,cpp}` | no | Shared UI helpers (button width, type badge / colour / countdown) |
| `NotesStore.{h,cpp}` | yes | Note + group model, CRUD, notes.json round-trip, export/import |
| `Markdown.{h,cpp}` | yes | Line parser (Paragraph/Heading/Bullet/Task; Text/Bold/Italic/Chip spans); task count/toggle/uncheck-all |
| `MarkdownEdit.{h,cpp}` | yes | Toolbar commands on a cell model: wrap bold/italic, toggle line prefixes, list continuation on Enter |
| `MarkdownRender.{h,cpp}` | no | ImGui view rendering: FontBig headings, faux bold (double draw), faux italic (shear), checkboxes, rich chips |
| `ChipTextEdit.{h,cpp}` | no | Multi-line stb_textedit editor with atomic chip cells (adapted from Pie UI), `ApplyEdit` for toolbar ops; soft wrap at the field width (`RowLen` -> `WrapRowLength` in `ChipCodec.h`, used by layout, caret, selection and drawing) |
| `ChipCodec.{h,cpp}` | yes | Text <-> cell round-trip (chat codes -> one chip cell), UTF-8 helpers |
| `ChipResolver.{h,cpp}` | yes | Offline structural label + colour from the codec (`[Item]`, `[Mirage Build]`) |
| `ChipColor.{h,cpp}` | yes | GW2 link colours (per type, per item rarity) |
| `ChipRich.{h,cpp}` | no | Rich chip: resolved name/icon/tooltip, right-click Copy code / Open wiki, Pie UI left-click |
| `DecoderClient.{h,cpp}` | no | Decoder Ring consumer |
| `PieUiClient.{h,cpp}` | no | Pie UI consumer (`OpenChatLink`) |
| `IconCache.{h,cpp}` / `IconUrl.{h,cpp}` | no / yes | URL -> texture via Nexus `Textures_*FromURL`, failure set (retryable); FNV-1a key, URL split |
| `Reset.{h,cpp}` | yes | GW2 daily/weekly reset times (UTC), countdown text |
| `I18n.{h,cpp}` | yes | English -> German UI string table, `Tr` / `TrN`, current language |
| `FontScale.{h,cpp}` | no | Scaled text without stretching the small font: `SetFontBase`, `FontScaleScope` |
| `Theme.{h,cpp}` | no | Accent palette (gold border, green/amber buttons, edit tint) |
| `chat/*` | yes | **Vendored** Pie UI codec — do not edit |

### Features

- GFM task lists `- [ ]` / `- [x]`: clickable in view mode (saves immediately), checked items dimmed
  and struck through (chips too: grey name only by default; `done_show_time` keeps the event
  countdown in a lighter event colour, `done_show_popups` keeps tooltip + right-click menu; Options > "Events", event chips only --
  `ChipDoneStyle` in `ChipDone.h`, set via `SetCheckedTaskChips`), `done/total` progress in the note list, context menu "Uncheck all tasks".
- Edit toolbar B / I / H1 / H2 / list / task + `Ctrl+B` / `Ctrl+I`, toggles off when reapplied.
- Enter continues bullet/task lists; Enter on an empty item ends the list.
- Optional side-by-side live preview, persisted in `settings.json`.
- Persistence is crash-safe (`SafeFile`): notes.json/settings.json are replaced atomically,
  notes.json keeps `notes.json.bak`; an unreadable notes.json is set aside as
  `notes.corrupt-N.json` (never overwritten), the backup is loaded and a notice is shown.
- Per-frame work is cached: markdown parse of the open note / preview (`ParseCache`), task
  progress per note (`TaskCache`, by body hash); `Tr()` uses a string_view table.
- UI code conventions: popups are requested with `requestPopup_(Popup::X)` from anywhere and
  opened at the end of `Render()`; new modals add a `Popup` value + title in `PopupTitle`.
  Child windows report clicks via the `*RequestId_` fields, handled in `handleRequests_()`.
- Note list width: splitter between list and note (drag, double-click resets to 200 px),
  `list_width` in `settings.json`, clamped only when drawn. Long titles / group names are cut
  with "..." (`NotesUi::Ellipsize`, also the view-mode title) and shown in full in a tooltip.
  List rows (`RowSelectable` in `NotesList.cpp`): own highlight, corners = `Theme::Rounding()`, inner
  padding, gap between rows and groups. Right-click on empty list space: new note / new group /
  delete notes. Buttons are opaque (Nexus's translucent ones are blended in `PushWindowStyle`);
  the edit toolbar wraps in a narrow pane. Corner rounding of the whole note window (window,
  panes, frames, popups, grabs, scrollbars, list rows): `corner_rounding` (0-12 px, default 0),
  Options > "Layout options" (slider + number field), pushed by `PushRounding` (from `PushWindowStyle`; settings + guide window via
  `Theme::PlainWindowScope`, which also makes their background opaque). ImGui's
  TextInput cursor shows as a gear in GW2: `AddonRender` swaps ours for the arrow.
- Note types Note/Daily/Weekly (`Note::type`, `Note::resetStamp` = last reset applied, both in
  notes.json + export). `Reset.{h,cpp}` (pure): last/next daily (00:00 UTC) and weekly
  (Mon 07:30 UTC) resets -- Nexus has no reset API. `NotesStore::ApplyResets(now, skipId)` runs
  every frame in `Render()` and skips the note open in the editor. `SetType` starts counting at
  the latest reset, so current ticks stay until the next one. List badge D/W + countdown tooltip.
- Group name colour: addon-wide `color_group_name` (Theme::UserColors, default = border) and per
  group `Group::hasColor/color` (notes.json + export, `null` = default; import keeps local colour of
  existing groups). Resolution in `NotesWindow::groupColor_`.
- Bulk delete: menu -> "Delete notes..." modal (`renderBulkDeleteModal_`, two steps: select, then
  confirm) -> `NotesStore::DeleteMany`; groups are left in place.
- Groups (`Trailpack::Group`: id, name, collapsed; one level): `Note::groupId`. notes.json is now
  `{"format":"trailpack-memo-notes","version":2,groups,notes}` (v1 plain array still loads).
  Export v2 carries `groups` + per-note `group`; `ExportGroupJson` for one group; import reuses a
  group by id or recreates it (only groups used by imported notes). Deleting a group ungroups its
  notes. List UI: `renderGroupHeader_` / `renderNoteRow_`, drag payload `TPM_NOTE`.
- Note ids are globally unique (32 hex, random 128 bit, `IsNoteId`); `FromJson` migrates legacy
  counter ids (`00000000`, old notes.json files) and `NotesWindow::Init` saves right after loading so ids
  stay stable.
- Menu -> "Settings..." opens an own settings window (`g_SettingsVisible`, dllmain) that renders
  `RenderOptions()`, the same function Nexus's options pane (`AddonOptions`) calls.
- Import / "Export all" live in a menu behind the square hamburger button next to "Create New
  Note" (single notes: right-click -> "Export note...").
- Export/import: `NotesStore::ExportJson` / `PrepareImport` / `AddImported` (pure, host-tested;
  format `trailpack-memo-export` v1 with ids; import also takes a bare `notes.json` array). Import
  shows a selection modal; duplicates (same id, or same title + body when the file has no valid
  id) are never added; an id match whose title/body differ (`ImportItem::changed`) is offered as
  an opt-in update (`UpdateImported`), locked while that note is open in the editor. Windows file dialogs run on a worker thread
  (`std::async`) and are polled in `Render()`.
- UI language English/German (`I18n.{h,cpp}`, pure, host-tested table). Auto = Nexus language via
  a `Localization_Set` probe key (the game's own language is not exposed to addons); `language`
  in `settings.json`. Chip structural labels (`[Item]`, spec names) come from the vendored codec
  and stay English; Decoder Ring names come in whatever language it delivers.
- User colours (border / background / text) in `Theme::Colors()`, pushed for the whole window by
  `Theme::PushWindowStyle()`; background/text only override Nexus's style once picked
  (`color_*` in `settings.json`, null = Nexus default). Daily / weekly badge background and letter
  colours (`color_daily_badge[_text]`, `color_weekly_badge[_text]`, defaults in `Theme::BADGE_*`)
  are read by `NotesUi::TypeColor` / `TypeTextColor`; the badge colour also tints the countdown.
  Window border on/off: `show_border` (`UserColors::showBorder`, `PushWindowStyle` sets size 0).
  List / note panes: `color_{list,note}_{background,text}` (`UserColors::*Bg/*Text`). A picked
  pane background is pushed as ChildBg for that child only; inside the note pane nested children
  get ChildBg = transparent, so the colour is exact (Nexus's own ChildBg is translucent and was
  stacked on WindowBg). Note text uses `Theme::TextColorScope` around title / body / editor /
  preview; chip tooltips and menus push `Theme::WindowText()` back. List text: `ListTextColor`.
  Pane borders: `show_{list,note}_border` + `color_{list,note}_border` (null = follow the window
  border colour), pushed via `Theme::PushListBorder/PushNoteBorder` around BeginChild only
  (ChildBorderSize + ImGuiCol_Border); the preview pane uses the note's border.
  All colour options are one `Trailpack::ColorSet` (`g_Settings.colors`; JSON keys stay flat);
  dllmain `ApplyColors` / `CollectColors` map it to `Theme::UserColors`. Borders default off.
  Themes: Default (= `ColorSet{}`), `ColorSet::Light()` / `Dark()` (window text stays Nexus's;
  editor fields on a custom note background use `Theme::FieldBg`), user themes in
  `settings.json` `themes` (`SaveTheme` / `DeleteTheme`). The combo shows the theme equal to the
  current colours, else "Custom". The settings and guide windows never take the user colours
  (plain Nexus style, readable with any theme). Secondary text in list / note (group arrow and
  count, task progress, done tasks, empty state) derives from the pane's text colour
  (`NotesUi::Dimmed`) instead of Nexus's fixed TextDisabled.
  Backup (Settings > Backup, dllmain): `Backup::Make/Parse`; restore validates notes + settings
  first, asks inline, then `NotesWindow::RestoreNotes` + settings from defaults. Default folder
  `addons/TrailpackMemo/backups/`.
  Menu button: `color_menu_button` / `color_menu_icon` (hover/pressed = `FieldBg` shades).
  Panes (list, note, preview) always have inner padding: `PushPaneBorder` also pushes
  WindowPadding, and they are begun with `Theme::kPaneFlags` (AlwaysUseWindowPadding) because
  ImGui drops the padding of border-less children.
  Popups (menus, tooltips, event picker, modals) of the note window: `color_popup_bg` (null =
  `Theme::DefaultPopupBg()`: the list colour as shown, opaque; pushed as ImGuiCol_PopupBg by
  `PushWindowStyle`) and `color_popup_text` (null = list text, else Nexus text);
  `PopupTextScope` also gives input fields a shade of the popup background; `Theme::PopupTextScope` first inside each `BeginPopup` block / around
  `BeginTooltip` and Combos, `Theme::Tooltip` instead of `ImGui::SetTooltip`, all modals via
  `renderModals_`. New popups must do the same.
  Ungrouped notes follow the groups without a heading; dropping a note on empty list space
  (`BeginDragDropTargetCustom` over the list) or "Remove from group" ungroups it.
  Options layout: label | swatch | Reset in fixed columns, subheadings Window / Note list /
  badges (2x2 table with preview), "Reset all colours" resets `Theme::UserColors`.
- Two text sizes (70-200 %, Nexus options > "Font options"; colours under "Colour options", both
  collapsible sections in `AddonOptions`): UI = `text_scale`, note content = `note_text_scale`,
  both applied with `Trailpack::FontScaleScope` (`FontScale.{h,cpp}`) -- the UI scope right after
  `Begin`, the note scope around view / editor / preview. It sets the font size directly
  (ImGui 1.80 inherits `SetWindowFontScale` only one child level deep) and draws enlarged text
  from Nexus's FontBig scaled down instead of stretching the 15 px font (less blur). Headings use
  `HeadingRatio()` so they stay larger than body text. Fully crisp text at every size would need
  a TTF loaded at the exact size via `Fonts_AddFromFile` (not done: Nexus's font isn't a file).

---

## Backlog

Each task: feature branch, Definition of Done from `AGENTS.md`, then tell the user what to verify
in game.

### B1 — WndProc key capture while editing
**Why:** `dllmain.cpp` has a TODO: while a chip editor is focused, held keys can leak to the game
(e.g. WASD moves, `H` opens Hero panel) because `WantTextInput` flickers.
**Files:** `src/dllmain.cpp` (uses `Trailpack::ChipInputActive()` / `Trailpack::RequestChipBlur()` from
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

### B5 — Event timers in notes (next big feature)
**Why:** world bosses and map metas follow fixed UTC schedules; notes should show them as chips
with a live countdown and copy the waypoint on click.
- [ ] Work the stages in [`EVENTS.md`](EVENTS.md) (decisions D1-D14 are agreed — ask before
      changing them). One feature branch + PR per stage.

### Ideas (not scheduled — only on user request)
- Search/filter in the note list; note reordering (drag or up/down).
- Export/import a single note as plain `.md` (JSON export/import exists).
- Pinning or a "daily routine" auto-uncheck at reset time.

---

## Changelog of this plan
- 2026-10-29 — Plan set up in the monorepo; rules in `AGENTS.md`.
- 2026-10-29 — B5 (event timers) planned with the user; details in `EVENTS.md`.
- 2026-10-29 — Standalone: fork references removed, namespace `Magpie` -> `Trailpack`; B3 / B4
  (upstream docs / sync) dropped.
