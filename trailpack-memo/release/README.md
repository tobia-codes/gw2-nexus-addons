# Trailpack Memo

A notepad for Guild Wars 2 as a [Raidcore Nexus](https://raidcore.gg/Nexus) addon: several notes
with markdown, checklists, groups and daily / weekly notes, right in the game. English and German.

<!-- Screenshots (plans/RELEASE.md): images/notes.png, images/tooltip.png, images/editor.png
![Notes with chat-link and event chips](images/notes.png)
-->

## Features

- **Notes** with markdown (bold, italic, headings, lists), a formatting toolbar and live preview.
- **Checklists** (`- [ ]` / `- [x]`), progress in the note list; **daily / weekly notes** untick
  themselves at the game's reset.
- **Groups**, drag and drop ordering, colours and themes, corner rounding, text sizes.
- **Chat-link chips**: paste `[&...]` links into a note; right-click to copy the code or open the
  wiki, left-click a waypoint to copy it.
- **Backup**, export and import of notes and settings.

### Extended features with Trailpack Core

Install [Trailpack Core](https://github.com/tobia-codes/trailpack-core) as well to unlock:

- **Names, icons and tooltips** for chat-link chips (item stats, rune / sigil bonuses, skills,
  traits, recipes, waypoints, builds).
- **Event timers**: world bosses and metas as live chips ("Tequatl the Sunless - in 23 min"),
  picked with the *Event* button or by typing `/e` and a name.

Without Core these are greyed out; everything else works. Core can be added any time, no restart.

## Install

In game: Nexus > Addon Library > *Trailpack Memo* > Install. Or download `TrailpackMemo.dll` from the
[latest release](../../releases/latest) into `Guild Wars 2/addons/`. Updates come through Nexus.

Toggle the window with the quick access icon or `CTRL+SHIFT+T`. Your notes are stored in
`addons/TrailpackMemo/`.

## Source and license

Source code: [tobia-codes/gw2-nexus-addons](https://github.com/tobia-codes/gw2-nexus-addons)
(this repository only holds the releases). MIT license, see [LICENSE](LICENSE).
