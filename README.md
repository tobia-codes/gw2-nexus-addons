# gw2-nexus-addons

[Raidcore Nexus](https://raidcore.gg/Nexus) addons for Guild Wars 2, in one repository.

| Addon | Description |
|-------|-------------|
| [Trailpack Memo](trailpack-memo/) | Notepad with markdown, **checklists**, a formatting toolbar, live preview and GW2 chat-link chips. Fork of PieOrCake's [Magpie Memo](https://github.com/PieOrCake/magpie_memo). |

## Layout

```
gw2-nexus-addons/
├── CMakeLists.txt        # builds every addon
├── shared/               # third-party code used by all addons
│   ├── imgui/            #   Dear ImGui 1.80 (matches Nexus)
│   └── include/          #   nexus/Nexus.h, nlohmann/json
└── <addon>/              # one folder per addon (src/, tests/, README, LICENSE)
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

## Adding an addon

1. Create `<addon-name>/` with its own `CMakeLists.txt` (copy `trailpack-memo/CMakeLists.txt` as a template).
2. Link `gw2_imgui` and `gw2_headers` for the DLL target, `gw2_headers` for host tests.
3. Add `add_subdirectory(<addon-name>)` to the root `CMakeLists.txt` and a row to the table above.
4. Give it a unique Nexus signature, data directory, keybind and texture identifiers.

## Licenses

Each addon carries its own `LICENSE`. Shared third-party code keeps its upstream license
(`shared/imgui/LICENSE.txt`, MIT headers in `shared/include/nlohmann`).
