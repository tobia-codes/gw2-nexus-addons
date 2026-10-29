# Trailpack Memo — Event timers in notes (backlog B5)

> Living plan for one feature, resumable from any session. Rules (build, code rules, i18n,
> workflow) are in [`../AGENTS.md`](../AGENTS.md); overall state in [`PLAN.md`](PLAN.md).
> Work the stages top-down, one feature branch + PR per stage, tick boxes (`- [x]`) as tasks
> land and record findings under "Open points / findings". Decisions below were agreed with the
> user on 2026-10-29 (grilling session) — change them only after asking.

## Goal

Notes can contain GW2 events with a fixed schedule (world bosses, map metas, ...). An event is
an inline chip that shows when it starts next ("in 23 Min") or how long it has been running
("seit 4 Min"); clicking it copies the event's waypoint chat code. Events are inserted from a
searchable, categorised picker in the edit toolbar or by typing `/e ` in the editor. Schedules
are static, so start times are computed locally from UTC — like the daily / weekly resets in
`src/Reset.{h,cpp}`. No network access at runtime.

## Decisions

| # | Topic | Decision |
|---|---|---|
| D1 | Scope | Every event with a fixed schedule on the wiki page "Event timers". Metas are split into real events only (bosses, finales, fights); pure states like "Daytime" / "Night" are left out. |
| D2 | Data source | Generated once by `tools/gen_events.py`: schedules from the English wiki, German names via the wiki's language links, waypoint chat codes from the official API. Output `src/events/EventData.h` is checked in. Re-run for new GW2 releases. |
| D3 | Manual fixes | `tools/event_overrides.json` (checked in) corrects single events (waypoint, names, category, exclude) and is applied on every generator run. Never hand-edit `EventData.h`. |
| D4 | Data check | After each generator run: a summary (count per category, events without waypoint, without German name) and host tests that pin the next start times of a few well-known events (e.g. Tequatl) to wiki values. The user spot-checks in game. |
| D5 | Categories | Wiki section order (core world bosses, hardcore world bosses, Living World S2-S4, Icebrood Saga, HoT, PoF, EoD, SotO, JW, later expansions). Expansion names / abbreviations stay as they are; generic category names are translated. *Later:* user-defined category order (see Ideas). |
| D6 | Names | German where the generator found one, else English; follows the addon language. Ids are always stable English slugs (`tequatl`, `dragonstorm`). |
| D7 | Note syntax | `{{event:<id>}}` in the note body, rendered as a chip; in the editor it is one atomic cell (deleted only as a whole), like chat-link chips. |
| D8 | Picker (toolbar) | "Event" button next to "[ ] Task". Opens a picker: search field on top (focused; matches name, map and id, German and English), categories below as collapsible sections; while searching only categories with hits, expanded. Rows show name + map (grey). **No countdown in the picker.** Click inserts the chip at the cursor. |
| D9 | Inline suggestions | Typing `/e ` or `/event ` in the editor opens a compact list at the cursor: flat, filtered by the text typed after it, max ~8 hits, name + grey category abbreviation. Arrow keys / Enter / Escape; Enter or click replaces the typed `/e ...` with the chip. The toolbar button stays. |
| D10 | Chip text | View + preview: "Tequatl · in 23 Min" before the start, "Tequatl · seit 4 Min" while running (EN: "in 23 min" / "for 4 min"). Format: seconds below 1 min ("in 45 s"), "Min" below 1 h, "1 Std 23 Min" above. **Editor: name only** (no ticking width, no cursor jumps). Unknown id: grey chip "Unknown event (id)", text kept as is. |
| D11 | Tooltip | Next three starts in local time, duration, map, waypoint name. |
| D12 | Click | Left click copies the waypoint chat code and shows "Waypoint copied" at the mouse for ~1.5 s. Right click: menu "Copy waypoint" / "Open in wiki". No Pie UI action for events. |
| D13 | Colours | New "Events" section in Colour options: "Upcoming" (default teal) and "Active" (default green). Part of `ColorSet`, so themes and backups include them. |
| D14 | Delivery | Six stages below, each its own feature branch + PR, each buildable, host-tested and checkable in game on its own. |

## Data model (target)

`src/events/EventData.h` — generated, pure (no ImGui / Nexus / Win32), sorted by category then
wiki order:

```cpp
namespace Magpie { namespace Events {

enum class Category : uint8_t { CoreBosses, HardcoreBosses, LivingWorld2, ..., JanthirWilds };

struct EventDef {
    const char* id;          // stable English slug, used in {{event:<id>}}
    const char* nameEn;
    const char* nameDe;      // nullptr = use nameEn
    const char* mapEn;
    const char* mapDe;       // nullptr = use mapEn
    Category    category;
    const char* waypoint;    // chat code "[&BN...]" or nullptr if unknown
    const char* waypointName;
    const char* wikiPage;    // English wiki page title
    uint16_t    cycleMinutes;   // schedule repeats every N minutes (e.g. 120, 180, 1440)
    uint16_t    durationMinutes;
    uint8_t     startCount;
    uint16_t    starts[24];     // start offsets in minutes after 00:00 UTC within one cycle
};

extern const EventDef kEvents[];
extern const size_t   kEventCount;

}} // namespace Magpie::Events
```

`cycleMinutes` + `starts[]` covers both evenly repeating metas (one offset, 120-min cycle) and
irregular world-boss rotations (several offsets, 1440-min cycle). If the wiki data needs more
(e.g. weekday-only events), extend the model in stage 1 and record it here.

## Code map (where things go)

| Piece | File(s) | Pure? |
|---|---|---|
| Generator + overrides | `tools/gen_events.py`, `tools/event_overrides.json` | — (Python 3, stdlib only) |
| Event table | `src/events/EventData.h` (+ `.cpp` if the table is large) | yes |
| Lookup, schedule maths, search, time text | `src/Events.{h,cpp}` (`Find(id)`, `State(def, now)` -> active / secondsUntil / secondsSince, `NextStarts(def, now, n)`, `Search(query, lang)`, `FormatEventTime`) | yes |
| `{{event:id}}` in the markdown parser | `src/Markdown.{h,cpp}` (new span kind `Event`) | yes |
| Atomic event cell in the editor | `src/ChipCodec.{h,cpp}` (text <-> cells), `src/ChipTextEdit.{h,cpp}` | codec yes |
| Chip drawing, tooltip, click, menu | new `src/EventChip.{h,cpp}` (modelled on `src/ChipRich.cpp`), called from `src/MarkdownRender.cpp` | no |
| Toolbar button + picker | `src/NotesDetail.cpp` (`renderEditToolbar_`), picker in a new `src/EventPicker.{h,cpp}` | no |
| `/e ` suggestions | `src/ChipTextEdit.cpp` (detect trigger, expose query + caret pos), list drawn by `EventPicker` | no |
| Colours | `src/Settings.{h,cpp}` (`ColorSet::eventUpcoming/eventActive`), `src/Theme.h` (`UserColors`), `src/dllmain.cpp` (`ApplyColors` / `CollectColors`, "Events" section) | settings yes |
| Texts | `src/I18n.cpp` (German lines), guide `src/HelpWindow.cpp`, `README.md` | — |

Reuse: `Reset::FormatCountdown` style and UTC handling in `src/Reset.cpp`; `ChipRich.cpp` for the
popup-id scoping inside the view child and the wiki-open helper; `Theme::TextColorScope` /
`WindowText()` so tooltips and menus keep readable text.

## Stages

### Stage 1 — Generator and data
- [ ] Inspect the wiki source of "Event timers" (MediaWiki API, `action=parse` / raw wikitext and
      the templates it uses) and record its structure under "Open points / findings".
- [ ] Write `tools/gen_events.py` (stdlib only): parse schedules, categories, durations, waypoint
      names and wiki page per event; German names via `prop=langlinks&lllang=de`; waypoint chat
      codes from `/v2/continents/1/floors/1` (`points_of_interest[].chat_link`, type `waypoint`)
      matched by name + map; derive stable ids (slug of the English name, unique).
- [ ] `tools/event_overrides.json` for fixes; generator applies it and prints the summary (D4).
- [ ] Generate `src/events/EventData.h`; decide header-only vs `.h`+`.cpp` by size.
- [ ] Short "how to re-run" note in this file and in `README.md` (developer section).

### Stage 2 — Schedule maths (pure, host-tested)
- [ ] `src/Events.{h,cpp}`: `Find`, `State`, `NextStarts`, `Search` (case-insensitive substring
      over name / map / id in both languages), `FormatEventTime` (D10 formats, EN + DE).
- [ ] Host tests: known starts (Tequatl and two more, pinned to wiki values), active window
      edges (start, start + duration - 1 s, end), cycle wrap at 00:00 UTC, search hits, unknown id.
- [ ] Add the new files to both CMake targets; list `Events` as a pure unit in `AGENTS.md`.

### Stage 3 — Chip in view, preview and editor
- [ ] Parser: `{{event:<id>}}` -> `Event` span (`Markdown.{h,cpp}`) + host tests.
- [ ] Codec / editor: one atomic cell per token (`ChipCodec`, `ChipTextEdit`); editor shows the
      name only (D10); round-trip host tests.
- [ ] `EventChip`: view / preview text with live countdown, unknown-id chip, tooltip (D11),
      left-click copy + "Waypoint copied" hint, right-click menu (D12).
- [ ] Guide + README lines; German texts.
- [ ] User test: write `{{event:tequatl}}` by hand, check view, preview, editor, click, menu.

### Stage 4 — Picker in the edit toolbar
- [ ] "Event" toolbar button, picker with search + collapsible categories, no countdown (D8).
- [ ] Insert at the cursor via `bodyEditor_.ApplyEdit`; refocus the editor afterwards.
- [ ] Category names: wiki order, expansion names unchanged, generic names translated (D5).
- [ ] User test: search "teq", collapse / expand, insert into an empty and a long note.

### Stage 5 — `/e ` suggestions in the editor
- [ ] Detect `/e ` / `/event ` before the caret in `ChipTextEdit`; expose query + caret screen pos.
- [ ] Compact list at the caret (D9); keys: Up / Down / Enter / Escape; mouse click.
- [ ] Replace the typed trigger + query with the event cell; Escape leaves the text as typed.
- [ ] Make sure the keys don't leak to the game or trigger toolbar shortcuts (see backlog B1).
- [ ] User test: type `/e teq`, pick with keys and with the mouse, cancel with Escape.

### Stage 6 — Colours and polish
- [ ] `ColorSet::eventUpcoming` / `eventActive` (+ JSON keys, equality, themes), `UserColors`,
      "Events" section in Colour options with Reset; Light / Dark theme values (D13).
- [ ] Host tests for the new settings keys.
- [ ] Final guide / README pass; `PLAN.md` "Current state" + B5 ticked.

## Ideas (noted, not scheduled — only on user request)
- Alert shortly before an event in a note starts (e.g. Nexus alert X minutes before).
- Next event countdown in the note list row (like the D / W badge).
- User-defined category order in the picker.

## Open points / findings
- (Stage 1) Wiki source structure of "Event timers": _to be recorded_.
- (Stage 1) Events that don't fit `cycleMinutes` + `starts[]` (if any): _to be recorded_.
