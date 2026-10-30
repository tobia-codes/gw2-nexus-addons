# Trailpack Memo — Working Plan

> Living plan: current state and open backlog. The rules (build, identity, code rules,
> optional dependencies) are in [`../AGENTS.md`](../AGENTS.md). Work the backlog top-down unless
> the user says otherwise, tick boxes (`- [x]`) as tasks land, and update "Current state" when the
> architecture changes.

---

## Current state (as of 2026-10-29)

Base (CRUD + persistence, view/edit + unsaved modal, markdown render, chips with Decoder Ring,
icon cache, Pie UI waypoint/open-link) and the features below are done. Standalone addon, C++
namespace `Trailpack`. Host tests: 81 test functions in
`tests/host_tests_main.cpp`, all green.

### Module map (`src/`)

Feature folders, includes relative to `src/` (e.g. `"core/Settings.h"`). `generated/` and
`vendor/` hold code that is never edited by hand (see AGENTS.md).

| Module | Pure? | Role |
|---|---|---|
| `dllmain.cpp` | no | Nexus lifecycle, identity, keybind, quick access, options pane; maps `Settings` <-> live state |
| `core/Settings.{h,cpp}` | yes | settings.json model with validation/clamping (`g_Settings`); `ColorSet` (all colour options) + built-in Light/Dark, user themes |
| `core/Backup.{h,cpp}` | yes | Backup file format (`trailpack-memo-backup` v1: notes + settings) and settings export (`trailpack-memo-settings` v1) |
| `core/SafeFile.{h,cpp}` | yes | Atomic write (+ `.bak`), read, set aside unreadable files; path helpers (UTF-8 strings, Win32 W-API / POSIX) replacing `std::filesystem` |
| `core/Reset.{h,cpp}` | yes | GW2 daily/weekly reset times (UTC), countdown text |
| `core/I18n.{h,cpp}` | yes | English -> German UI string table, `Tr` / `TrN`, current language |
| `notes/NotesWindow.{h,cpp}` | no | Note state, load/save, navigation, frame layout, popup requests (`requestPopup_` / `beginModal_`), caches |
| `notes/NotesList.cpp` | no | Left column: groups, note rows, menu button, splitter |
| `notes/NotesDetail.cpp` | no | Right pane: view / edit modes, toolbar, rendered body |
| `notes/NotesModals.cpp` | no | Confirmations, group name / colour / delete, bulk delete, notice |
| `notes/NotesImportExport.cpp` | no | File dialogs (worker thread), export, import selection |
| `notes/NotesUi.{h,cpp}` | no | Shared UI helpers (button width, type badge / colour / countdown) |
| `notes/NotesStore.{h,cpp}` | yes | Note + group model, CRUD, notes.json round-trip, export/import |
| `markdown/Markdown.{h,cpp}` | yes | Line parser (Paragraph/Heading/Bullet/Task; Text/Bold/Italic/Chip spans); task count/toggle/uncheck-all |
| `markdown/MarkdownEdit.{h,cpp}` | yes | Toolbar commands on a cell model: wrap bold/italic, toggle line prefixes, list continuation on Enter |
| `markdown/MarkdownRender.{h,cpp}` | no | ImGui view rendering: FontBig headings, faux bold (double draw), faux italic (shear), checkboxes, rich chips |
| `chips/ChipTextEdit.{h,cpp}` | no | Multi-line stb_textedit editor with atomic chip cells (adapted from Pie UI), `ApplyEdit` for toolbar ops; soft wrap at the field width (`RowLen` -> `WrapRowLength` in `chips/ChipCodec.h`, used by layout, caret, selection and drawing) |
| `chips/ChipCodec.{h,cpp}` | yes | Text <-> cell round-trip (chat codes -> one chip cell), UTF-8 helpers |
| `chips/ChipResolver.{h,cpp}` | yes | Offline structural label + colour from the codec (`[Item]`, `[Mirage Build]`) |
| `chips/ChipColor.{h,cpp}` | yes | GW2 link colours (per type, per item rarity) |
| `chips/ChipRich.{h,cpp}` | no | Rich chip: resolved name/icon/tooltip, right-click Copy code / Open wiki, Pie UI left-click |
| `chips/IconCache.{h,cpp}` / `chips/IconUrl.{h,cpp}` | no / yes | URL -> texture via Nexus `Textures_*FromURL`, failure set (retryable); FNV-1a key, URL split |
| `events/Events.{h,cpp}` | yes | Event timers (B5): lookup, UTC schedule maths, search, chip time text |
| `integrations/DecoderClient.{h,cpp}` | no | Decoder Ring consumer |
| `integrations/PieUiClient.{h,cpp}` | no | Pie UI consumer (`OpenChatLink`) |
| `ui/FileDialog.{h,cpp}` | no | Windows open/save dialog (worker thread), JSON filter; used by export/import and backups |
| `ui/HelpWindow.{h,cpp}` | no | "?" title-bar button + guide window (sections translated via `Tr`; update it with new features) |
| `ui/FontScale.{h,cpp}` | no | Scaled text without stretching the small font: `SetFontBase`, `FontScaleScope` |
| `ui/Theme.{h,cpp}` | no | Accent palette (gold border, green/amber buttons, edit tint) |
| `generated/EventData.{h,cpp}` | yes | **Generated** event table (`tools/gen_events.py`) — never edit by hand |
| `vendor/chat/*` | yes | **Vendored** Pie UI codec — do not edit |

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
  notes. List UI: `renderGroupHeader_` / `renderNoteRow_`, drag payloads `TPM_NOTE` / `TPM_GROUP`.
  Reorder by drag and drop: list order = store order (`notes_` / `groups_`, saved in notes.json);
  `MoveNoteNextTo` (note onto note: before / after by row half, takes the target's group),
  `MoveGroupNextTo` (header onto header; below all rows = last), `MoveToGroup` puts the note last
  in its new group. Drops are collected in `listDrop_` and applied after the list is drawn
  (`applyListDrop_`); targets use `AcceptBeforeDelivery` to draw an insert line.
  Backup / export keep the order (store order); import: existing notes and groups keep their
  place (local order wins), new groups follow in the file's group order (`ImportItem::groupOrder`),
  new notes go last in their group in file order (`test_import_keeps_order`).
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
  Editor field frame: `show_editor_border` (default off) + `color_editor_border` (null = window
  border), Colour options > "Note editor"; passed as `borderCol` to `ChipTextEdit::Render` (0 = none).
  All colour options are one `Trailpack::ColorSet` (`g_Settings.colors`; JSON keys stay flat);
  dllmain `ApplyColors` / `CollectColors` map it to `Theme::UserColors`. Borders default off.
  Themes: Default (= `ColorSet{}`), `ColorSet::Light()` / `Dark()` (window text stays Nexus's;
  editor fields on a custom note background use `Theme::FieldBg`), user themes in
  `settings.json` `themes` (`SaveTheme` / `DeleteTheme`). The combo shows the theme equal to the
  current colours, else "Custom". The settings and guide windows never take the user colours
  (plain Nexus style, readable with any theme). Secondary text in list / note (group arrow and
  count, task progress, done tasks, empty state) derives from the pane's text colour
  (`NotesUi::Dimmed`) instead of Nexus's fixed TextDisabled.
  Backup (Settings > Data, dllmain): `Backup::Make/Parse`; restore validates notes + settings
  first, asks inline, then `NotesWindow::RestoreNotes` + settings from defaults. Default folder
  `addons/TrailpackMemo/backups/`.
  Settings window order: "General" (open: quick access icon, language, Decoder Ring status),
  Font, Layout, Colour, Events; below a separator "Data: backup and transfer" (backup + settings
  export, file actions apart from the settings).
  Settings export / import (Settings > Data, dllmain): `Backup::MakeSettings`
  / `ParseSettings` (also reads a full backup's settings); same dialog flow (`FileJob`), import
  confirmed inline, then `ReplaceSettings` (from defaults, like a restore); notes untouched.
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
  Ungrouped notes (no heading) come before the groups by default; `groups_first` (Settings,
  Layout options > "Groups above single notes", `NotesWindow::SetGroupsFirst`) puts the groups
  first. Row spacing: `list_row_spacing` (0-20 px, default 3; Layout options > "List row spacing",
  `NotesWindow::SetRowSpacing`, used by `RowGap`). Dropping a note on empty list space
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
- [ ] Work the stages in [`EVENTS.md`](EVENTS.md) (decisions D1-D15 are agreed — ask before
      changing them). One feature branch + PR per stage.

### B6 — Switch to Trailpack Core
**Why:** Decoder Ring is replaced by our own required data service Trailpack Core; event data
moves there too. Planned in [`../trailpack-core/plans/PLAN.md`](../trailpack-core/plans/PLAN.md)
(decisions D15-D18 and D21 concern Memo; work happens in Core stages 1, 2 and 5). D21: afterwards
Memo references no other creator; the Pie UI integration is dropped, the LICENSE line stays.
- [ ] Follow the Core stages; do not extend `DecoderClient` or `generated/EventData` meanwhile.

### Ideas (not scheduled — only on user request)
- Search/filter in the note list.
- Export/import a single note as plain `.md` (JSON export/import exists).
- Pinning or a "daily routine" auto-uncheck at reset time.
- **Event table as a separate addon** (discussed 2026-10-30): an in-game overview of all events
  that highlights events from open (unchecked) tasks in notes. Event data and schedule maths
  come from Trailpack Core (see `../trailpack-core/plans/PLAN.md`, D13 / D14), not from a
  static library. The memo publishes the open event ids via Nexus
  events (broadcast on change + answer to a ping, like Decoder Ring / Pie UI); the payload is a
  plain C struct with a version field in a `public/` header, copied by the receiver. Optional
  back channel: click in the table opens the note. Not reading `notes.json` directly.

---

## Changelog of this plan
- 2026-10-30 — B6 added: switch from Decoder Ring to Trailpack Core (plan in `trailpack-core/`);
  the event-library idea is replaced by Core.
- 2026-10-30 — Notes and groups reorderable by drag and drop (was under Ideas).
- 2026-10-30 — B5 stages 1-5 done (user tests passed; stage 5: inline `/e ` suggestions); open point on D13 before stage 6; event
  table addon noted under Ideas. `src/` grouped into feature folders (`generated/`, `vendor/`).
- 2026-10-30 — DLL size 1.87 MB -> 0.95 MB: streams and `std::filesystem` replaced by `SafeFile`,
  own `__gnu_cxx::__verbose_terminate_handler` (drops the demangler). `--gc-sections` made it
  bigger, `-flto` crashes MinGW GCC 13 (ICE) -- both not used.
- 2026-10-29 — Plan set up in the monorepo; rules in `AGENTS.md`.
- 2026-10-29 — B5 (event timers) planned with the user; details in `EVENTS.md`.
- 2026-10-29 — Standalone: fork references removed, namespace `Magpie` -> `Trailpack`; B3 / B4
  (upstream docs / sync) dropped.
