# Trailpack Core

The data service of the Trailpack addons for Guild Wars 2, a [Raidcore Nexus](https://raidcore.gg/Nexus)
addon without a window of its own. It unlocks the extended features of the other Trailpack
addons, e.g. names, icons, tooltips and event timers in
[Trailpack Memo](https://github.com/tobia-codes/trailpack-memo). English and German.


## What it does

- Turns GW2 chat links into **names, icons and tooltips**: items (stats, rarity, rune / sigil
  bonuses), skins, skills (incl. mount / transform skills and effects), traits and recipes.
- **Built-in data that works offline**: waypoints and points of interest with their map, build
  links (elite specialisation), event timers for world bosses and metas.
- **Plays by the rules**: only the official GW2 API and the GW2 wiki, never touches the game, no
  API key, bundled requests far below the API rate limit, answers cached on disk.

Options (Nexus > Options > Trailpack Core): status, "Offline only", cache size, "Clear cache".

## Install

In game: Nexus > Addon Library > *Trailpack Core* > Install. Or download `TrailpackCore.dll` from the
[latest release](../../releases/latest) into `Guild Wars 2/addons/`. Updates come through Nexus.
Data is stored in `addons/TrailpackCore/`.

## Source and license

Source code: [tobia-codes/gw2-nexus-addons](https://github.com/tobia-codes/gw2-nexus-addons)
(this repository only holds the releases). MIT license, see [LICENSE](LICENSE).
