# Trailpack Memo — Agent Instructions

Rules for AI coding agents working in `trailpack-memo/`. Current state and open tasks live in
[`plans/PLAN.md`](plans/PLAN.md) — read it before starting a task and update it when one lands.

## What this is

A Raidcore Nexus addon for Guild Wars 2: a notepad with multiple markdown notes, task-list
checkboxes, a formatting toolbar, live preview, and inline GW2 chat-link "chips" (icon + name +
tooltip). It is a **standalone fork** of [PieOrCake/magpie_memo](https://github.com/PieOrCake/magpie_memo)
and runs **side by side** with Magpie Memo. Shared third-party code (ImGui 1.80, nlohmann/json,
`nexus/Nexus.h`) is in `../shared/`.

## Build & test (run from the repo root)

```
cmake -S . -B build && cmake --build build --target TrailpackMemo           # -> build/TrailpackMemo.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                       # pure-logic host tests
```

- Toolchain: `x86_64-w64-mingw32-g++`, `cmake`, `g++` (WSL). If one is missing, the agent has no
  sudo — ask the user to run `sudo apt install mingw-w64 cmake g++ make`.
- **Definition of Done:** DLL builds clean **and** host tests green. In-game behaviour can only be
  verified by the user — say what to test, never claim it works in game.

## Fork identity (must stay distinct from Magpie Memo)

| Thing | Value | Where |
|---|---|---|
| Name / window | `Trailpack Memo` | `src/dllmain.cpp` (`WINDOW_NAME`) |
| Signature | `0x54504D6D` ("TPMm"; Magpie = `0x4D61704D`) | `src/dllmain.cpp` |
| DLL | `TrailpackMemo.dll`, export via `TrailpackMemo.def` | `CMakeLists.txt` |
| Data dir | `addons/TrailpackMemo/` (`notes.json`, `settings.json`) | `Paths_GetAddonDirectory("TrailpackMemo")` |
| Toggle keybind | `CTRL+SHIFT+T` (Magpie: `CTRL+SHIFT+M`) | `src/dllmain.cpp` |
| Texture ids | prefix `TPM_ICON_` | `src/IconCache.cpp` |
| Author | `PieOrCake.7635 & tobia-codes` | `src/dllmain.cpp` |
| Localization keys | `TPM_LANG_PROBE` (active-language probe), `KB_TRAILPACK_MEMO_TOGGLE` (keybind name) | `src/dllmain.cpp` |
| Updates | `UP_None`, `UpdateLink = nullptr` (see PLAN backlog B2) | `src/dllmain.cpp` |

New identifiers that the game or Nexus can see (signature, ids, events, textures, paths) must
**not** collide with Magpie Memo's.

## Code rules

- Internal C++ namespace stays **`Magpie`** (deliberately, to ease upstream merges). Mentions of
  "Magpie Memo" in comments that refer to the upstream addon are correct — leave them.
- **Vendored codec is byte-identical:** `src/chat/ChatLinks.{h,cpp}`, `src/chat/SpecData.h` come
  from Pie UI. Never edit them; a codec bug is fixed upstream and re-vendored.
- **Pure units stay pure** (no ImGui/Nexus/Win32 includes) so the host-test target builds:
  `NotesStore`, `Settings`, `Backup`, `Events` (+ generated `events/EventData`), `Markdown`,
  `MarkdownEdit`, `ChipCodec`, `ChipResolver`, `ChipColor`, `IconUrl`, `chat/ChatLinks`. Put new
  logic into a pure unit + host test (`tests/host_tests_main.cpp`) where possible.
- **UI text is translated (English + German):** wrap every user-visible literal in
  `Magpie::I18n::Tr("English text")` and add the German line to the table in `src/I18n.cpp`
  (same printf specifiers — host-tested). Plurals via `TrN`, popups via `PopupName` (stable
  `###id`), button widths via `ButtonWidth` (German labels are longer).
- **Rendered characters:** ASCII plus Latin-1 letters (German umlauts/ß — Nexus's own German
  locale uses them). No em-dashes, arrows, Unicode bullets or other symbols.
- **No game-memory reading, no game-function hooks.** Anything that seems to need it is out of scope.
- NotesWindow is split by area (see the header comment in `src/NotesWindow.h`). Open modals only
  via `requestPopup_` / render them with `beginModal_`; never call `ImGui::OpenPopup` for a modal
  from inside a child window. Settings belong in `Settings` (pure, tested), not in new globals.
- **Popups in the note window** (menus, context menus, tooltips, pickers, modals) use the popup
  colours: `Theme::PopupTextScope` first inside each `if (ImGui::BeginPopup...(...)) {` (or around
  `BeginTooltip` / a Combo) and `Theme::Tooltip(...)` instead of `ImGui::SetTooltip`; modals are
  covered by `renderModals_`. The background comes from `Theme::PushWindowStyle`.
- New user-visible features also get a line in the in-game guide (`src/HelpWindow.cpp`, plus the
  German text in `I18n.cpp`) and in `README.md`.
- Match the surrounding style, naming and comment density. Keep `README.md` (addon + root table)
  in sync with user-visible changes.

## Optional dependencies (never crash when absent)

Decoder Ring and Pie UI may be missing, or load/unload mid-session.

- **Decoder Ring** (`public/DecoderRingApi.h`, `src/DecoderClient.cpp`): never cache
  `DecoderRingApi*`; call `GetDecoder()` (DataLink `DECODER_RING_API` + `apiVersion == 1`) before
  every use; presence is live, not latched; on `EV_DECODER_RING_UNLOADING` drop the record cache and
  pending keys; the `EV_DECODER_RING_RESOLVED` payload is only valid during the handler — copy it
  out; check `DecoderRecord.schemaVersion`. Build links (`0x0D`) need the full chat-code string.
- **Pie UI** (`src/PieUiClient.cpp`): presence latched on `EV_PIE_UI_READY`; `OpenChatLink` raises
  `EV_PIEUI_OPEN_CHATLINK` with the `[&...]` string — fire-and-forget, a no-op when Pie UI is absent.

## Workflow

- **Never deploy the DLL** into the game folder — the user does that.
- **Never bump the `V_*` version** defines in `src/dllmain.cpp` unless asked.
- Commit/push only when asked, on a feature branch (not `main`).
