# Trailpack Core

A headless [Raidcore Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2: the data service
every Trailpack addon builds on. It draws nothing in game — it only has a page in Nexus'
options.

- **Chat links to names, icons and tooltips:** items (stats, rarity, rune / sigil bonuses),
  skins, skills (incl. mount / transform skills and effects via the GW2 wiki), traits and
  recipes (ingredients, required rating). Answers come from a disk cache and are fetched in the
  background when new.
- **Built in, works offline:** waypoints and points of interest (with their map), build links
  (elite specialisation) and the event timers (world bosses, metas, ...).
- **English and German**, following the Nexus language.
- **Plays by the rules:** it only talks to the official GW2 API and the GW2 wiki, never touches
  the game, needs no API key, bundles requests and stays far below the API rate limit.

Options (Nexus > Options > Trailpack Core): status, "Offline only" (no network at all — cached
and built-in data only), cache size and "Clear cache". Data lives in `addons/TrailpackCore/`.

## For addon developers

Include [`public/TrailpackCoreApi.h`](public/TrailpackCoreApi.h) — the header comment explains
the handshake (READY / UNLOADING / PING), the `structSize` rule and resolving.

## Build

From the repository root (see the [root README](../README.md)):

```
cmake -S . -B build && cmake --build build --target TrailpackCore      # -> build/TrailpackCore.dll
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host && ctest --test-dir build-host
```

Game data is regenerated after content patches with `python3 trailpack-core/tools/gen_gamedata.py`
and `python3 trailpack-core/tools/gen_events.py` (sources in each script's header).

## License

MIT, see [LICENSE](LICENSE).
