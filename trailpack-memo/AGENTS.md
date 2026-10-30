# Trailpack Memo — Agent Instructions

Rules for AI coding agents working in `trailpack-memo/`. Current state and open tasks live in
[`plans/PLAN.md`](plans/PLAN.md) — read it before starting a task and update it when one lands.

## What this is

A Raidcore Nexus addon for Guild Wars 2: a notepad with multiple markdown notes, task-list
checkboxes, a formatting toolbar, live preview, and inline GW2 chat-link "chips" (icon + name +
tooltip), plus event timer chips. Shared third-party code (ImGui 1.80, nlohmann/json,
`nexus/Nexus.h`) is in `../shared/`.

## Build & test (run from the repo root)

```
cmake -S . -B build && cmake --build build --target TrailpackMemo           # -> build/TrailpackMemo.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                       # pure-logic host tests
```

- Toolchain: `x86_64-w64-mingw32-g++`, `cmake`, `g++` (WSL). If one is missing, the agent has no
  sudo — ask the user to run `sudo apt install mingw-w64 cmake g++ make`.
- **Definition of Done:** host tests (incl. new ones for the change) green, **then** the DLL
  builds clean; `README.md` (and `release/README.md` if the player-facing text changes) and the
  `## Unreleased` section of `CHANGELOG.md` updated for player-visible changes. In-game behaviour can only be
  verified by the user — say what to test, never claim it works in game.

## Identity

| Thing | Value | Where |
|---|---|---|
| Name / window | `Trailpack Memo` | `src/dllmain.cpp` (`WINDOW_NAME`) |
| Signature | `0x54504D6D` ("TPMm") | `src/dllmain.cpp` |
| DLL | `TrailpackMemo.dll`, export via `TrailpackMemo.def` | `CMakeLists.txt` |
| Data dir | `addons/TrailpackMemo/` (`notes.json`, `settings.json`) | `Paths_GetAddonDirectory("TrailpackMemo")` |
| Toggle keybind | `CTRL+SHIFT+T` | `src/dllmain.cpp` |
| Texture ids | prefix `TPM_ICON_` | `src/chips/IconCache.cpp` |
| Author | `tobia-codes` | `src/dllmain.cpp` |
| Localization keys | `TPM_LANG_PROBE` (active-language probe), `KB_TRAILPACK_MEMO_TOGGLE` (keybind name) | `src/dllmain.cpp` |
| Updates | `UP_GitHub`, release repo `tobia-codes/trailpack-memo` (tag `memo-v<version>` here, see `.github/workflows/release.yml`) | `src/dllmain.cpp` |

## Code rules

- C++ namespace: **`Trailpack`**.
- **Source layout:** own code in feature folders under `src/` (`core/`, `notes/`, `markdown/`,
  `chips/`, `events/`, `integrations/`, `ui/`; `dllmain.cpp` at the root). Include own headers
  relative to `src/` (`#include "core/Settings.h"`). `src/generated/` is written by `tools/*.py` --
  never edit it by hand.
- **Chat-link codec:** `libs/trailpack-chatlinks` (`#include "chatlinks/ChatLinks.h"`,
  `Trailpack::ChatLinks`), shared with the other Trailpack addons and tested there. Codec changes
  go into the library with a test in its own suite.
- **Pure units stay pure** (no ImGui/Nexus/Win32 includes) so the host-test target builds:
  `NotesStore`, `Settings`, `Backup`, `Events`, `Markdown`,
  `MarkdownEdit`, `ChipCodec`, `ChipResolver`, `ChipColor`, `IconUrl`. Put new
  logic into a pure unit + host test (`tests/host_tests_main.cpp`) where possible.
- **UI text is translated (English + German):** wrap every user-visible literal in
  `Trailpack::I18n::Tr("English text")` and add the German line to the table in `src/core/I18n.cpp`
  (same printf specifiers — host-tested). Plurals via `TrN`, popups via `PopupName` (stable
  `###id`), button widths via `ButtonWidth` (German labels are longer).
- **Rendered characters:** ASCII plus Latin-1 letters (German umlauts/ß — Nexus's own German
  locale uses them). No em-dashes, arrows, Unicode bullets or other symbols.
- **No `<fstream>`/`<sstream>`/`<iostream>`/`<filesystem>`** in addon code: with the static
  libstdc++ they pull ~1 MB of locale code into the DLL. Files and paths go through `SafeFile`
  (UTF-8 `std::string` paths; `FileDialog::Widen`/`Narrow` at the dialog boundary).
- **No game-memory reading, no game-function hooks.** Anything that seems to need it is out of scope.
- NotesWindow is split by area (see the header comment in `src/notes/NotesWindow.h`). Open modals only
  via `requestPopup_` / render them with `beginModal_`; never call `ImGui::OpenPopup` for a modal
  from inside a child window. Settings belong in `Settings` (pure, tested), not in new globals.
- **Popups in the note window** (menus, context menus, tooltips, pickers, modals) use the popup
  colours: `Theme::PopupTextScope` first inside each `if (ImGui::BeginPopup...(...)) {` (or around
  `BeginTooltip` / a Combo) and `Theme::Tooltip(...)` instead of `ImGui::SetTooltip`; modals are
  covered by `renderModals_`. The background comes from `Theme::PushWindowStyle`.
- New user-visible features also get a line in the in-game guide (`src/ui/HelpWindow.cpp`, plus the
  German text in `I18n.cpp`) and in `README.md`.
- Match the surrounding style, naming and comment density. Keep `README.md` (addon + root table)
  in sync with user-visible changes.

## Trailpack Core (extended features; never crash when absent)

Core may be missing, too old, or load/unload mid-session. Everything goes through
`src/integrations/CoreClient.cpp` (header: `../trailpack-core/public/TrailpackCoreApi.h`):

- Never keep the `TpcApi*`; re-resolve it (DataLink `TRAILPACK_CORE_API`, `apiVersion >=
  CoreClient::kMinApiVersion`) before every use. Presence is live (`CoreClient::Status()`).
- On `EV_TRAILPACK_CORE_UNLOADING` / `_READY` cached answers are dropped; the `_RESOLVED` payload
  is only valid during the handler — copy it. Set `structSize` before every call.
- The event list is rebuilt only in `CoreClient::Frame()` (render thread, start of `AddonRender`).
- Without Core, Core features are **greyed out, not hidden** (Core PLAN D17): the toolbar Event
  button (`Theme::LockedScope` + a tooltip that works when disabled), the "Events" settings, `/e`
  suggestions; chips show structural labels. Hints only in Settings > General and the guide.
- No references to other creators' addons or repositories (Core PLAN D21); the MIT line in
  `LICENSE` stays.

## Workflow

- **Never deploy the DLL** into the game folder — the user does that.
- **Never bump the `V_*` version** defines in `src/dllmain.cpp` unless asked.
- Commit/push only when asked, on a feature branch (not `main`).
