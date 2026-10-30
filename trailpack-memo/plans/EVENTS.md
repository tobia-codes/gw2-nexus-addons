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
`src/core/Reset.{h,cpp}`. No network access at runtime.

## Decisions

| # | Topic | Decision |
|---|---|---|
| D1 | Scope | Every event with a fixed schedule on the wiki page "Event timers". Metas are split into real events only (bosses, finales, fights); pure states like "Daytime" / "Night" are left out. |
| D2 | Data source | Generated once by `tools/gen_events.py`: schedules from the English wiki, German names via the wiki's language links, waypoint chat codes from the official API. Output `src/generated/EventData.h` is checked in. Re-run for new GW2 releases. |
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
| D15 | Rotating events | An event that moves between places on a fixed rotation (Ley-Line Anomaly, Fractal Incursion, Dragon Bash Hologram Stampede) is ONE event with several places, not one per place. Chip, tooltip and left-click use the place of the running occurrence, and switch to the next place only after that run has ended. The tooltip names the map of each listed start. (User, 2026-10-29.) |

## Data model (as generated in stage 1)

`src/generated/EventData.h` (types) + `src/generated/EventData.cpp` (tables) — generated, pure (no
ImGui / Nexus / Win32), in wiki order (category, then meta, then segment):

```cpp
namespace Trailpack { namespace Events {

constexpr int kDayMinutes = 1440;

struct CategoryDef { const char* nameEn; const char* nameDe; };   // nameDe nullptr: expansion name

struct PlaceDef {                    // where an occurrence happens
    const char* mapEn;  const char* mapDe;      // nullptr = none known / use English
    const char* waypoint;                       // chat code "[&B...]", nullptr = none
    const char* waypointNameEn; const char* waypointNameDe;
};

struct EventDef {
    const char*     id;              // stable English slug, used in {{event:<id>}}
    const char*     nameEn;
    const char*     nameDe;          // nullptr = use nameEn
    uint8_t         category;        // index into kCategories
    const char*     wikiPage;        // English wiki page (may contain "#section")
    uint8_t         placeCount;      // >= 1; > 1 for rotating events (D15)
    const PlaceDef* places;
    uint8_t         count;           // occurrences per day
    const uint16_t* starts;          // minutes after 00:00 UTC, ascending
    const uint16_t* durations;       // minutes, parallel to starts
    const uint8_t*  placeOf;         // index into places, parallel to starts
};

extern const CategoryDef kCategories[]; extern const size_t kCategoryCount;
extern const EventDef    kEvents[];     extern const size_t kEventCount;

}} // namespace Trailpack::Events
```

Every event repeats **daily** (the wiki widget restarts its sequence at 00:00 UTC), so a plain
list of start minutes per day fits every event; no cycle field is needed. An occurrence may run
past midnight (`start + duration > 1440`, e.g. Dragon's Stand at 23:30 for 120 min). Durations
are per occurrence because they can differ within one event; so is the place (D15).

## Code map (where things go)

| Piece | File(s) | Pure? |
|---|---|---|
| Generator + overrides | `tools/gen_events.py`, `tools/event_overrides.json` | — (Python 3, stdlib only) |
| Event table | `src/generated/EventData.h` (+ `.cpp` if the table is large) | yes |
| Lookup, schedule maths, search, time text | `src/events/Events.{h,cpp}` (`Find(id)`, `State(def, now)` -> `Status{active, start, end}`, `NextStarts(def, now, n)`, `Name/Map/CategoryName(…, lang)`, `Search(query)`, `FormatTime(seconds, active)`) | yes |
| `{{event:id}}` in the markdown parser | `src/markdown/Markdown.{h,cpp}` (new span kind `Event`) | yes |
| Atomic event cell in the editor | `src/chips/ChipCodec.{h,cpp}` (text <-> cells), `src/chips/ChipTextEdit.{h,cpp}` | codec yes |
| Chip drawing, tooltip, click, menu | new `src/chips/EventChip.{h,cpp}` (modelled on `src/chips/ChipRich.cpp`), called from `src/markdown/MarkdownRender.cpp` | no |
| Toolbar button + picker | `src/notes/NotesDetail.cpp` (`renderEditToolbar_`), picker in a new `src/events/EventPicker.{h,cpp}` | no |
| `/e ` suggestions | `src/chips/ChipTextEdit.cpp` (detect trigger, expose query + caret pos), list drawn by `EventPicker` | no |
| Colours | `src/core/Settings.{h,cpp}` (`ColorSet::eventUpcoming/eventActive`), `src/ui/Theme.h` (`UserColors`), `src/dllmain.cpp` (`ApplyColors` / `CollectColors`, "Events" section) | settings yes |
| Texts | `src/core/I18n.cpp` (German lines), guide `src/ui/HelpWindow.cpp`, `README.md` | — |

Reuse: `Reset::FormatCountdown` style and UTC handling in `src/core/Reset.cpp`; `ChipRich.cpp` for the
popup-id scoping inside the view child and the wiki-open helper; `Theme::TextColorScope` /
`WindowText()` so tooltips and menus keep readable text.

## Stages

### Stage 1 — Generator and data
- [x] Inspect the wiki source of "Event timers" and record its structure (see findings).
- [x] Write `tools/gen_events.py` (stdlib only): schedules, categories, names, waypoint chat codes
      and wiki page from the widget's `data.json`; German names from the German widget script
      (matched by chat code); waypoint + map names (EN/DE) from `/v2/continents/1/floors/*`;
      ids = slug of the English event name (unique, else an override).
- [x] `tools/event_overrides.json` for fixes; generator applies it and prints the summary (D4).
- [x] Generate `src/generated/EventData.{h,cpp}` (`.h` + `.cpp`: ~100 events); both CMake targets.
- [x] Host test `test_event_data` (ids unique, starts ascending, Tequatl and Dragon's Stand
      pinned to the wiki schedule).
- [x] "How to re-run" note (below) and in `README.md` (Building).

**Re-run the generator** (from the repo root; downloads are cached in `tools/.cache/`, delete it
to refetch):

```
python3 trailpack-memo/tools/gen_events.py            # live wiki + API
python3 trailpack-memo/tools/gen_events.py \
  --en-data   "http://web.archive.org/web/20260920230502id_/https://wiki.guildwars2.com/index.php?title=Widget:Event_timer/data.json&action=raw" \
  --de-script "http://web.archive.org/web/20250909200816id_/https://wiki-de.guildwars2.com/wiki/Widget:Event-Timer/script.js"
```

Then check the summary, run the host tests, and review the diff of `src/events/`.

### Stage 2 — Schedule maths (pure, host-tested)
- [x] `src/events/Events.{h,cpp}`: `Find`, `State` (active + start/end, or next start), `NextStarts`,
      `Name` / `Map` / `CategoryName` (DE falls back to EN), `Search` (case-insensitive substring
      over name / map / id in both languages, umlauts folded), `FormatTime` (D10 formats via
      `I18n::Tr`, EN + DE).
- [x] Host tests: known starts (Tequatl, Triple Trouble, Dragon's Stand), active window
      edges (start, start + duration - 1 s, end), day wrap at 00:00 UTC, an occurrence across
      midnight, search hits, unknown id, time texts in both languages.
- [x] Daylight saving: all schedule maths in UTC only (`int64_t` Unix seconds, like `Reset.cpp`;
      no `localtime` / `mktime` / TZ in `Events.cpp`). Host test: the same results with the
      process time zone set to a DST zone (e.g. `TZ=Europe/Berlin`) and to UTC, across both
      2026 switch dates (29 Mar, 25 Oct). (`test_events_ignore_time_zone`, POSIX hosts only.)
- [x] Add the new files to both CMake targets; list `Events` as a pure unit in `AGENTS.md`.

### Stage 3 — Chip in view, preview and editor
- [x] Token helpers in `Events` (`Token`, `FindToken`, `IsToken`; id = a-z 0-9 '-').
- [x] Parser: `{{event:<id>}}` -> `Event` span (`Markdown.{h,cpp}`) + host tests.
- [x] Codec / editor: one atomic cell per token (`ChipCodec`, `ChipTextEdit`, also on paste);
      editor shows the name only (D10); round-trip host tests.
- [x] `EventChip`: view / preview text with live countdown, unknown-id chip, tooltip (D11),
      left-click copy + "Waypoint copied" hint, right-click menu (D12). Separator in the chip
      text is " - " (AGENTS: no symbols like the middle dot outside letters).
- [x] Tooltip local times: convert **each** start's Unix time on its own with `localtime`
      (Windows applies DST per date), never a fixed UTC offset -- so a start after the next
      daylight-saving switch shows the right hour. Countdowns stay UTC-only.
- [x] Guide + README lines; German texts.
- [ ] User test: write `{{event:tequatl-the-sunless}}` by hand, check view, preview, editor,
      click, menu.

### Stage 4 — Picker in the edit toolbar
- [x] "Event" toolbar button, picker with search + collapsible categories, no countdown (D8):
      `src/events/EventPicker.{h,cpp}` (`EventPicker::Render(query, focusSearch)` -> picked id; Enter
      takes the first hit, Escape closes). Popup `##event_picker` in the detail child's scope.
- [x] Insert at the caret with `bodyEditor_.InsertChip(Events::Token(id))` (`ApplyEdit` only
      handles text cells); refocus the editor afterwards.
- [x] Category names: wiki order, expansion names unchanged, generic names translated (D5) --
      from the generated data (`Events::CategoryName`).
- [x] Rotating events (D15, from the user test): generator `"merge": true` per meta in the
      overrides -> one event with `places` + `placeOf`; `Events::State` / `NextOccurrences`
      carry the place; `Maps` lists all maps (picker), `Search` matches any map. 93 events now.
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
- (Stage 1, 2026-10-29) **Source structure.** The page "Event timers" renders a JS widget; all
  data is in `Widget:Event_timer/data.json` (`config.version`, `events.<metaKey>` with
  `category`, `name`, `segments.<r>` {`name`, `chatlink`, `link`} and `sequences` {`partial`,
  `pattern`} of `{r, d}`). Each UTC day starts with `partial`, then `pattern` repeats; segment
  `0` / empty names are fillers. The German wiki has its own widget (`Widget:Event-Timer/
  script.js`, JS object literal) with German names and the same chat codes.
- (Stage 1) **Wiki down.** Both wikis showed "The Guild Wars Wikis are undergoing maintenance"
  (still on 2026-10-29). The data was generated from Wayback Machine snapshots: English
  `data.json` of **2026-09-20** (version v5.4), German script of **2025-09-09**. Re-run against
  the live wiki once it is back and review the diff.
- (Stage 1) **Result:** 101 events in 13 categories (wiki order: Core Tyria, LW2, HoT, LW3, PoF,
  LW4, Icebrood Saga, EoD, SotO, JW, Visions of Eternity, Public Instances, Special Events).
  Hard world bosses are inside "Core Tyria". 13 events have no waypoint in the wiki (PvP
  tournaments, Awakened invasion, Crash Site, Verdant Brink day/night, Tangled Depths outposts).
  52 have no German name yet (newer content and the formatted names the old German script
  doesn't cover) -- English is shown (D6); add `name_de` overrides or re-run on the live German
  wiki later.
- (Stage 1) **Selection (D1) as applied in the overrides:** left out the time ruler and the three
  day/night rows, Auric Basin "Reset", Tangled Depths "Prep", Dragon's End "Preparations";
  Dragon's Stand "(continued)" joins the running event (id `dragons-stand`). Location-named
  segments are formatted with their meta ("Ley-Line Anomaly (Timberline Falls)"). PvP automated
  tournaments (EU / NA) and festival events (Special Events) are included -- they have a fixed
  time; festivals only run during their festival.
- (Stage 1) The model is daily lists of starts + durations (see Data model); no event needed
  more.
- (Stage 4, user test) The Ley-Line Anomaly showed as three events. Rotating events are now
  merged (D15): 93 events; ids `ley-line-anomaly`, `fractal-incursion`,
  `dragon-bash-hologram-stampede` replace the per-place ids from stage 1 (an old per-place token
  shows as "unknown event").
