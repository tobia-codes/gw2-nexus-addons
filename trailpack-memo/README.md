# Trailpack Memo

A notepad addon for Guild Wars 2 ([Raidcore Nexus](https://raidcore.gg/Nexus)): several notes with
markdown, checklists, groups and daily / weekly notes, GW2 chat links as inline chips and live event
timers — right in the game. English and German.

Toggle the window with the quick access icon or `CTRL+SHIFT+T`. Data lives in `addons/TrailpackMemo/`.

## Extended features with Trailpack Core

[Trailpack Core](../trailpack-core/) unlocks names, icons and tooltips for chat-link chips and the
event timers. Without it Memo works as a full notepad: chips show basic labels like `[Item]`
(copy / wiki still work) and the event features are greyed out with a hint. Core can be loaded or
unloaded at any time — nothing breaks, no restart needed.

## Features

### Notes and editing
- **Two panes**: note titles on the left, the selected note on the right; drag the divider to resize
  the list (double-click resets), long titles show in full on hover.
- **Markdown**: bold, italic, headings, bullet lists. **View / edit modes** with Save / Cancel and an
  unsaved-changes prompt; the editor wraps long lines (chips move as a whole).
- **Toolbar** in edit mode: **B**, *I*, H1, H2, list, task — on the selection or the current lines,
  toggles off when applied again; `Ctrl+B` / `Ctrl+I`. Lists continue on Enter; Enter on an empty
  item ends the list.
- **Live preview** beside the editor (remembered).

### Checklists, daily and weekly notes
- `- [ ] todo` / `- [x] done`; click a box in view mode to tick it (saved at once). Checked items
  are dimmed and struck through; the note list shows progress (`3/7`, green when complete).
- Right-click a note -> **Uncheck all tasks**.
- Note type **Daily** or **Weekly**: its boxes untick themselves after the GW2 reset (daily 00:00 UTC,
  weekly Monday 07:30 UTC), also when the game was closed over the reset. A *D* / *W* badge marks
  them in the list; hover for the countdown.

### Chat-link chips
- Paste any GW2 chat link (`[&...]`) into a note and it becomes a chip.
- With Trailpack Core: **icon, name and tooltip** — item stats, rarity, flavour text and rune / sigil
  bonuses, skill and trait facts, recipes (ingredients, required rating, the crafted item), the map
  of a waypoint, the elite specialisation of a build (`[Mirage Build]`).
- **Right-click**: *Copy chat code* / *Open in wiki*. **Left-click a waypoint chip** to copy its code.

### Event timers (with Trailpack Core)
- Insert an event with *Event* in the edit toolbar (search by name or map, or browse the
  categories), type `/e` and a name in the editor (e.g. `/e teq`; arrow keys + Enter or click,
  Escape closes the list), or write `{{event:<id>}}`.
- The chip shows a live countdown: "Tequatl the Sunless - in 23 min", "- for 4 min" while it runs.
  Hover for the next starts in local time, duration, map and waypoint; left-click copies the
  waypoint, right-click offers *Copy waypoint* / *Open in wiki*.
- ~90 fixed-schedule events (world bosses, metas, ...), computed locally in UTC — no internet
  needed. Rotating events (e.g. the Ley-Line Anomaly) are one chip that follows the current place.
- In checked tasks event chips become grey names; Options > "Events" can keep the countdown and /
  or tooltip and menu.

### Organising
- **Groups**: collapsible, coloured, rename / export / delete (notes are kept) via right-click; move
  notes by right-click -> *Move to group* or by dragging; ungrouped notes first (or groups first,
  *Layout options*).
- **Drag and drop** order for notes and groups, saved in `notes.json`.
- **Delete several notes at once** (menu button -> *Delete notes...*).

### Look and settings
- **Colours** for window, note list, note, editor, popups and the daily / weekly badges; themes
  *Default*, *Light*, *Dark* or your own (*Save as theme*).
- **Two text sizes** (UI and note content, 70-200 %), **corner rounding** (0-12 px) and **list row
  spacing** (0-20 px).
- **English and German UI**, following the Nexus language or set in the options.
- Options in Nexus' addon options or in a window of their own (menu button -> *Settings...*); the
  *?* next to the window title opens an in-game guide.

### Your data
- Notes are saved crash-safe in `addons/TrailpackMemo/notes.json` (previous version kept as
  `notes.json.bak`; a damaged file is set aside, never overwritten).
- **Export / import** notes (all, one note or one group) — duplicates are skipped, a changed version
  can be applied with *Update*.
- **Backup** of all notes, groups and settings, and **export / import of the settings** alone
  (Settings > "Data: backup and transfer").

## Install

In game: Nexus > Addon Library > *Trailpack Memo* (once it is listed), or put `TrailpackMemo.dll` from
the [releases](https://github.com/tobia-codes/trailpack-memo/releases) into `Guild Wars 2/addons/`.
Updates come through Nexus. For the extended features install Trailpack Core the same way.

## Building

Part of the [gw2-nexus-addons](../README.md) monorepo — build from the repository root:

```
cmake -S . -B build-host -DGW2_HOST_TESTS=ON && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                     # host tests first
cmake -S . -B build && cmake --build build --target TrailpackMemo           # -> build/TrailpackMemo.dll
```

Event data and schedules come from Trailpack Core (`trailpack-core/tools/gen_events.py`). The icon
(`icon.png` + `src/generated/EmbeddedIcon.h`) is drawn by `python3 trailpack-memo/tools/make_icon.py`.

## License

MIT — see [`LICENSE`](LICENSE).
