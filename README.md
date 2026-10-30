# gw2-nexus-addons

[Raidcore Nexus](https://raidcore.gg/Nexus) addons for Guild Wars 2, in one repository.

| Addon | Description |
|-------|-------------|
| [Trailpack Memo](trailpack-memo/) | Notepad with markdown, **checklists**, a formatting toolbar, live preview and GW2 chat-link chips. Event timer chips with live countdowns. |
| [Trailpack Core](trailpack-core/) *(in development)* | Headless data service of the Trailpack addons, unlocks their extended features: chat-link names, icons and tooltips from the GW2 API, built-in event and waypoint data. |

## Layout

```
gw2-nexus-addons/
├── CMakeLists.txt        # builds every addon
├── shared/               # third-party code used by all addons
│   ├── imgui/            #   Dear ImGui 1.80 (matches Nexus)
│   └── include/          #   nexus/Nexus.h, nlohmann/json
├── libs/                 # own libraries shared by the addons
│   ├── trailpack-chatlinks/  # GW2 chat-link codec
│   ├── trailpack-files/  #   crash-safe file I/O (SafeFile)
│   └── trailpack-ui/     #   shared ImGui look (palette, buttons, settings pages)
├── CLAUDE.md             # repo-wide notes for AI agents
└── <addon>/              # one folder per addon (src/, tests/, README, LICENSE,
                          #   AGENTS.md + CLAUDE.md for agents, plans/PLAN.md)
```

## Building

Cross-compiled to Windows DLLs from Linux/WSL with MinGW:

```
sudo apt install mingw-w64 cmake g++ make         # once

cmake -S . -B build && cmake --build build        # -> build/<Addon>.dll
```

Copy the DLL(s) into your Guild Wars 2 `addons` folder.

Host-side unit tests (native compiler, no Windows needed):

```
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host && ctest --test-dir build-host --output-on-failure
```

## Releasing

`python3 tools/release.py <core|memo> <version>` prepares the changelog and version, tags it and
(after asking) pushes; `.github/workflows/release.yml` then publishes the DLL in the addon's
release repository. Every player-visible change goes under `## Unreleased` in the addon's
`CHANGELOG.md`. Details: `plans/RELEASE.md`.

## Adding an addon

1. Create `<addon-name>/` with its own `CMakeLists.txt` (copy `trailpack-memo/CMakeLists.txt` as a template).
2. Link `gw2_imgui` and `gw2_headers` for the DLL target, `gw2_headers` for host tests.
3. Add `add_subdirectory(<addon-name>)` to the root `CMakeLists.txt` and a row to the table above.
4. Give it a unique Nexus signature, data directory, keybind and texture identifiers.
5. Add `AGENTS.md` (rules for AI agents), `plans/PLAN.md` (state + backlog) and a `CLAUDE.md`
   containing `@AGENTS.md` and `@plans/PLAN.md`.
6. Add `CHANGELOG.md` (with `## Unreleased`), `release/README.md` for its release repository, and
   the addon to `tools/release.py` and `.github/workflows/release.yml`.

## Licenses

Each addon carries its own `LICENSE`. Shared third-party code keeps its upstream license
(`shared/imgui/LICENSE.txt`, MIT headers in `shared/include/nlohmann`).
