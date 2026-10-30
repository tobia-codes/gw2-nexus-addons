# Trailpack Core — Working Plan

> Living plan: decisions, stages and backlog. The rules (build, identity, code rules, API
> etiquette) are in [`../AGENTS.md`](../AGENTS.md). Work the stages in order, tick boxes
> (`- [x]`) as tasks land, and update "Current state" when the architecture changes.

---

## Current state (as of 2026-10-30)

Stages 1-5 done (libraries, pure core, DLL, Memo on Core); stage 6 (release) open.
Decisions D1-D21 were agreed with the user on 2026-10-30 — ask before changing any of them.

### Module map (`src/`)

| Module | Pure? | Role |
|---|---|---|
| `dllmain.cpp` | no | Nexus lifecycle, DataLink table, worker thread, RESOLVED pump, settings page |
| `api/Service.{h,cpp}` | yes | The ABI's behaviour: routes to Offline / Events / Resolver, copies into caller structs (`structSize`) |
| `resolve/Resolver.{h,cpp}` | yes | Queue, batching, token bucket, retries, wiki fallback, recipe step, cache (de)serialisation |
| `resolve/Parse.{h,cpp}` | yes | API batch + wiki SMW parsers, fact formatting, recipe record |
| `resolve/Offline.{h,cpp}` | yes | PoI and build answers from `generated/GameData` |
| `resolve/Labels`, `resolve/Text`, `resolve/Record.h` | yes | Own DE / EN labels, markup stripping, UTF-8 truncation, internal record |
| `events/EventSchedule.{h,cpp}` | yes | Event lookup, schedule maths, texts, search (from Memo's `events/Events`) |
| `net/TokenBucket.{h,cpp}` | yes | Request budget + backoff (D10) |
| `net/HttpWinInet.{h,cpp}` | no | The DLL's HttpFetch (WinINet, User-Agent) |
| `core/Settings`, `core/I18n`, `core/Lang.h` | yes | settings.json, settings page texts, DE / EN |
| `generated/EventData`, `generated/GameData` | yes | **Generated** (`tools/gen_events.py`, `tools/gen_gamedata.py`) — never edit |

**What Core is:** a headless Nexus addon and the central data service for all Trailpack addons.
It turns GW2 chat links into display data (name, icon, tooltip) and serves the static game data
the addons need (events, waypoints, specialisations). It replaces the third-party Decoder Ring
(`PieOrCake/decoder_ring`), which Trailpack Memo uses today. Core is a new implementation; Decoder
Ring's code was only read for orientation (D2).

---

## Decisions

### Addon and role
- **D1 — Identity:** name `Trailpack Core`, folder `trailpack-core/`, `TrailpackCore.dll`,
  data dir `addons/TrailpackCore/`. Further identity values in `AGENTS.md`. Named "Trailpack
  Codex" at first, renamed because its main role is being the required base of every addon.
- **D2 — New implementation**, not a fork. Decoder Ring (MIT) serves as orientation only; no code
  is copied, so no upstream copyright line is needed.
- **D3 — Headless:** no window, no keybind. Only a settings page in Nexus' options
  (`RT_OptionsRender`), styled like Trailpack Memo (shared helpers in `libs/trailpack-ui/`):
  status (online / offline, queue length, requests in the last minute, last error), active
  language, cache size per language + "Clear cache", toggle "Offline only" (no network, cache and
  built-in data only).
- **D4 — The data service of every Trailpack addon** and their single data source (clarified with
  the user 2026-10-30: an addon works without Core; Core unlocks its extended features, which are
  greyed out until then — wording everywhere "extended features with Trailpack Core"):
  - built-in (generated) data: events, waypoints / PoIs, specialisation names;
  - API data: item, skin, skill (with wiki fallback for skills missing from `/v2/skills`), trait,
    recipe.
  - Trading-post prices: backlog.
  Nexus has no addon dependencies (`AddonDefinition` has no such field), so "required" means:
  consumers never crash without Core and tell the user what is missing (D17).

### ABI
- **D5 — Own ABI**, no Decoder Ring compatibility: DataLink `TRAILPACK_CORE_API`, events
  `EV_TRAILPACK_CORE_READY` / `_UNLOADING` / `_PING` / `_RESOLVED`. The header lives in
  `trailpack-core/public/TrailpackCoreApi.h`; consumers in this repo include it from there
  (no copy). Pure: `<cstdint>` only.
- **D6 — Records:** fixed-size PODs with `structSize` as the first field. The caller passes its
  size, Core fills only that much; fields are only ever appended. Common head (`linkType`, `id`,
  `status`, `lang`, `name`, `iconUrl`) plus clearly named per-type fields (e.g.
  `recipe.outputItemId`, `item.rarity`, `poi.mapName`) — no reused fields.
  The API table carries `apiVersion` first; consumers check a minimum version.
- **D7 — No pointers across the boundary:** every answer (names, search hits, occurrences) is
  copied into caller-owned buffers (fixed `char` arrays). A pointer into Core memory would be
  dangling the moment Core unloads. Lifetime rules as in Decoder Ring: re-resolve the API pointer
  before every use (never cache it), READY / UNLOADING / PING handshake, the RESOLVED payload is
  valid only during the handler.
- **D8 — Sync vs. async:** built-in data is answered synchronously in the same call. API data:
  warm cache -> answered at once; cold -> `NotReady`, fetch queued, `EV_TRAILPACK_CORE_RESOLVED`
  fires when it lands. Lookups work by `(linkType, id)` — a chat link is only one way to get the
  id; the full chat code is needed for build links only.
  Besides the single `Resolve` the ABI has **`ResolveMany(linkType, ids[], count, out[])`** for
  lists: one call, one lock, all cold ids queued at once (they end up in the same `?ids=` batch);
  each record carries its own status, cold ones arrive via the RESOLVED event as usual.

### Internals and rules
- **D9 — Architecture:**
  1. one worker thread with one shared queue (not one per type and language);
  2. batching: collect pending ids for a short window (~50 ms) and fetch up to 200 per request
     (`/v2/items?ids=...`);
  3. HTTP via WinINet (Nexus only loads textures from URLs, not JSON);
  4. disk cache via `SafeFile` (no streams / `std::filesystem`), one file per language and type
     under `addons/TrailpackCore/cache/<lang>/`, throttled writes;
  5. pure core (parsers, fact formatting, cache, batch planner, token bucket, event schedule and
     search) with host tests; HTTP is an injected function in tests.
- **D10 — GW2 rules (no risk for the account):** Core never touches the game (no memory reading,
  no hooks, no input) — only the public API and the wiki (Raidcore addon policy tier 0). The API
  limit is a token bucket of 300 burst / 5 per second **per IP**, shared with every other tool on
  the network, so Core stays far below: own token bucket (<= 2 requests/s, burst 20), batching,
  exponential backoff on 429 / 5xx, failure cooldown per id. Without a user-entered API key no
  account endpoints are called (optional key: backlog "Account data").
  Honest User-Agent `TrailpackCore/<version> (+https://github.com/tobia-codes/gw2-nexus-addons)`.
  Wiki requests only as the skill fallback, throttled the same way.
  Sources: <https://wiki.guildwars2.com/wiki/API:Best_practices>,
  <https://raidcore.gg/gw2/addon-policy>, <https://help.guildwars2.com/hc/en-us/articles/360013625034>.
- **D11 — Cache refresh:** on start fetch `/v2/build` once; after a game build change, entries
  count as stale and are refetched in the background on next access (the old value is served
  until then). Entries also expire after 30 days.
- **D12 — Languages: German and English only.** Core follows the Nexus language; anything else
  falls back to English. The active language is exposed in the API and in every record.
- **D13 — Events and waypoints are always built in** (generated, never fetched at runtime), so
  they keep working when the API is unreachable. New data ships with a Core release.
  Generators: `tools/gen_events.py` (moved from Memo, with `event_overrides.json`),
  `tools/gen_waypoints.py` (`/v2/continents/{1,2}/floors/*`, DE + EN), specialisation names
  (`/v2/specializations`, DE + EN). Output in `src/generated/`.
  Rule of thumb for future data: built in when small, bounded, changed only by content patches or
  expensive to fetch live; API + cache when huge, changing often, cheap per id and only a small
  part is ever needed.
- **D14 — Events split:** Core provides data and schedule maths: event list, categories, places
  and waypoints (DE / EN), `State` (active / upcoming), `NextOccurrences`, `Search` (for the `/e`
  suggestions). Memo keeps its editor part: `{{event:id}}` token syntax, `/e` trigger detection,
  time formatting and the popover UI.

### Trailpack Memo
- **D15 — Decoder Ring removed completely** (`public/DecoderRingApi.h`, `DecoderClient`, README,
  AGENTS, settings status); `integrations/CoreClient` replaces it. `generated/EventData` and the
  event generator move to Core.
- **D16 — What stays in Memo:** reset logic (`core/Reset`, daily / weekly notes must work without
  Core). The chat-link codec becomes the library `libs/trailpack-chatlinks/` (pure logic, needed
  on every keystroke, used by every addon); specialisation names move out of it into Core.
- **D17 — Memo without Core (or with a too old Core):** notes, checklists, daily / weekly keep
  working. Core features are **greyed out, not hidden**:
  - toolbar event button greyed out, hover text "Unlocked with Trailpack Core";
  - settings section "Events" greyed out with a note that Trailpack Core unlocks it;
  - `/e` suggestions off, no event countdowns;
  - chips show the structural labels (`[Item]`, `[Waypoint]`).
  Hint "More features with Trailpack Core: ..." + download link as a permanent line in Settings >
  General and a section in the guide (`HelpWindow`) — no banner in the note window. Too old:
  "Trailpack Core is too old (vX needed)". Loading Core later enables everything without restart.
  ImGui 1.80 has no `BeginDisabled`: use `PushItemFlag(ImGuiItemFlags_Disabled)` + reduced alpha,
  hover text via `ImGuiHoveredFlags_AllowWhenDisabled` (helper in `libs/trailpack-ui/`).
- **D18 — Prefetch:** Memo requests rune / sigil upgrades of an item chip when the chip is drawn,
  not only on hover (today `ChipRich.cpp` resolves them inside the tooltip).

- **D21 — No third-party references in Memo:** after the switch Memo names no other creator's
  repository or addon. The Pie UI integration (open the world map from a waypoint chip) is
  dropped, not rebuilt — it would need input to the game, which we rule out. The only remaining
  trace is the MIT copyright line `PieOrCake` in `trailpack-memo/LICENSE`, which must stay as long
  as Memo contains code derived from Magpie Memo.

### Process
- **D19 — Stages** below, one feature branch + PR per stage.
- **D20 — Update channel:** one release-only GitHub repository per addon, fed by the release
  workflow of this monorepo; addons use `UP_GitHub` (details in stage 6).

---

## Stages

Each stage: feature branch, Definition of Done from `AGENTS.md`, then tell the user what to verify
in game.

### Stage 1 — `libs/trailpack-chatlinks/` (done)
- [x] New static library `trailpack_chatlinks` (`Trailpack::ChatLinks`): base64, type of a link,
      decode / encode item (count, skin, upgrades) and build (profession, specs + traits, skills,
      profession bytes, weapon / override extension), encode by id (skill, trait, skin, recipe,
      map, outfit, achievement), English type labels + profession names, `Split` of a line into
      plain runs and links. No specialisation names (D16). Own host suite; cross-checked against
      the old codec with 200,000 random links (identical except that build links shorter than
      the fixed 44 bytes are now rejected).
- [x] Root `CMakeLists.txt` adds `libs/`; Memo links the library, `src/vendor/chat/` removed,
      Memo's AGENTS / PLAN / README updated.
- Left out on purpose: Alter Ego "AE2:" codes (another creator's format, D21) and URL spans —
      Memo keeps URLs literal itself (`Markdown.cpp`); wardrobe templates are classified but not
      decoded (nothing reads them). Offline build chips now read "[Mesmer Build]"; the elite
      spec name comes with the resolved record (Decoder Ring today, Core in stage 5).

### Stage 2 — `libs/trailpack-ui/` (done)
- [x] Static library `trailpack_ui` (`Trailpack::Ui`, `#include "tpui/Style.h"`, DLL builds
      only): palette (gold border, green / amber buttons, frame colours, status colours), button
      helpers, corner rounding, `BeginWindow`, `PlainWindowScope`, `HelpMarker`, and
      `LockedScope` + `LockedHint` for greyed-out items (D17; `PushItemFlag(Disabled)` + half
      alpha, hover via `AllowWhenDisabled`).
- [x] Memo's `ui/Theme` keeps only Memo's own colours and reaches the shared parts as
      `Theme::X` (`using namespace Trailpack::Ui`); no visual change.

### Stage 3 — Core: pure part (done)
- [x] `trailpack-core/CMakeLists.txt` (DLL + host tests), `add_subdirectory` in the root.
- [x] Generators + data: events (moved from Memo incl. overrides and `plans/EVENTS.md` data
      notes), waypoints / PoIs DE + EN, specialisation names DE + EN.
- [x] Event schedule + search (from Memo's `events/Events`, without the editor parts).
- [x] API parsers (item, skin, skill + wiki fallback, trait, recipe) into record fields; fact
      formatting.
- [x] Cache model (D11), batch planner, token bucket + backoff (D10) — all host-tested with an
      injected fetch.
- [x] Every case of the test checklist below has a host test (`tests/core_tests.cpp`, also run
      clean under ASan / UBSan / TSan), except the breakbar enrichment (backlog).
- Notes from building it:
  - Shared file helpers moved to `libs/trailpack-files` (`SafeFile`, used by Memo and Core).
  - Event data is generated into Core by `tools/gen_events.py` (copy of Memo's generator, same
    sources, identical data); Memo keeps its own copy until stage 5 removes it.
  - `tools/gen_gamedata.py` reads every continent floor once (English, ~90 MB, cached in
    `tools/.cache/`) and German only for the floors that name a PoI: 3240 PoIs, 168 maps,
    81 specialisations, all with German names.
  - The wiki was in maintenance while the fixtures were captured: the wiki tests use
    hand-written bodies in the SMW ask shape plus the captured maintenance page (which must
    fail cleanly). Replace them with captured responses once the wiki answers again.
  - Wiki fallback names are English in both languages (the German wiki has no game-id pages).
  - German item labels written by Core (`resolve/Labels.cpp`: slots, "Benötigte Stufe", ...) are
    our best knowledge of the in-game wording — check them in game.

#### Test checklist (from Decoder Ring's host tests)
Cases Decoder Ring (`PieOrCake/decoder_ring`, `tests/test_main.cpp`) learned the hard way. Our
tests cover the same cases with **our own** fixtures: capture fresh, untrimmed responses from the
API / wiki (EN and DE) into `tests/fixtures/`, never copy Decoder Ring's test code (D2). Cases
marked *(DR feature)* only apply once we build that feature.

**ABI / offline data**
- [x] Records and API table are POD / trivially copyable; layout pinned (`structSize`, offsets).
- [x] Build link -> spec label ("Mirage Build" for spec 59; core profession -> "Mesmer Build").
- [x] Waypoint / PoI ids resolve from the built-in table with name, map and kind
      (waypoint / point of interest / vista), DE and EN.

**Items (`/v2/items`)**
- [x] Armour (e.g. Zojja's Breastplate): defense, attributes incl. `CritDamage` -> "Ferocity",
      infusion slot, weight, subtype, required level, flavour text, bound on acquire.
- [x] Weapon (e.g. Judgment Trident): weapon strength line, **no** defense line; flavour markup
      (`<c=@flavor>`, `<br>`) stripped cleanly.
- [x] Rune (e.g. Superior Rune of the Scholar): `details.bonuses` listed verbatim (the consumer
      shows them for the upgrade ids of the parent item, D18).
- [x] Mystic Coin: `NoSell` must **not** make it untradeable (it is traded on the TP).
- [x] Every rarity tier maps; unknown / missing rarity stays "unknown", never a real tier.
- [x] Subtype wording: `HelmAquatic` -> "Aquatic Helm", generic `*Aquatic` rule, `Default` is
      dropped, real slots (`Coat`, `Rifle`, `Trident`, `Speargun`) pass through.
- [x] German response: our own labels are translated, names from the API used verbatim; the
      request URL carries `lang`.
- [x] Tooltip lines are capped (fact count and text length) without cutting UTF-8 characters.

**Skills (`/v2/skills` + wiki fallback)**
- [x] Normal skill: name, icon, description, facts.
- [x] Skill missing from `/v2/skills` (mount / siege turtle / transform): wiki fallback via the
      SMW `action=ask` API — e.g. 63475 "Slam" with description and facts ("Damage", condition
      with duration, "Number of Targets", "Defiance Break", "Range"); wiki facts carry no icons.
- [x] Name from the canonical name, not the page title (no "(turtle)" / "(effect)" suffix).
- [x] Id collision: 63440 is also an item ("Defender's Staff"); the query must be limited to the
      skill context so it resolves to "Open Access".
- [x] Wiki markup cleanup: bold, HTML entities, `[[Category:...]]` must not leak into facts
      (e.g. "FireCategory:Fire field skills").
- [x] Empty wiki result (`[]`) -> failed, plain `[Skill]` label, never an invented name.
- [x] Effect / buff links (food, guild / WvW boosts) are 0x06 links but neither in `/v2/skills`
      nor in the skill wiki space: second fallback under the effect context — e.g. 46587
      "Malnourished", 35126 "Guild WvW Reward Track Boost".
- [ ] *(DR feature)* Breakbar enrichment from the wiki: 6154 Overcharged Shot gets
      "Defiance Break: 232" (the wiki text has a stray "332" from a "sic" note — read 232);
      5492 Fire Attunement gets none, never "Defiance Break: 0"; no double line, no extra
      fetch when the fact already exists; label translated, number from the English wiki.

**Traits, recipes, skins**
- [x] Trait 214 "Raging Storm": description, icon, facts "Recharge: 8s" (`value`),
      "Radius: 360" (falls back to `distance`), "Number of Targets: 5", "Fury (4s)" (buff with
      duration); API facts carry icons.
- [x] Recipe 9508: output item 62905 "Elegant Huntsman's Backpack", ingredients + required
      rating; output and ingredients fetched in **one** `/v2/items?ids=` batch.
- [x] Guild recipe 11777 (output 77721 "Silver Tequatl Trophy", rating 350): guild-upgrade
      ingredients via `/v2/guild/upgrades?ids=` (best effort).
- [x] Recipe name localized ("Rezept: ..." in German).
- [x] Skin: name, icon; URL carries `lang`.

**Cache, language, lifetime**
- [x] Cache round-trip per type; an old cache format is refetched, never poisons the cache;
      a missing new field reads back as "unknown".
- [x] Language switch never serves the warm English entry for a German request (cache
      namespaced by language).
- [x] Nexus language -> API language mapping (anything but DE -> EN, D12).
- [x] Invalid id (e.g. 0) fails synchronously, no request.
- [x] **Every request ends in exactly one terminal outcome** (sync answer or event): resolved,
      failed after timeout, invalid, in cooldown — none stays "not ready" forever.
- [x] Failure cooldown: a failed id is not refetched before the cooldown ends.
- [x] Teardown while a fetch is in flight waits for it and emits nothing afterwards; calls after
      teardown are safe.

### Stage 4 — Core DLL (done; in-game test open)
- [x] `public/TrailpackCoreApi.h` (D5-D8, incl. `ResolveMany`) with layout tests.
- [x] WinINet fetch, worker thread, disk cache via `SafeFile`, `/v2/build` check.
- [x] Nexus lifecycle: DataLink, READY / UNLOADING / PING, RESOLVED from the main-thread pump,
      Nexus language -> DE / EN.
- [x] Settings page (D3): General (status, language, game build, requests per minute,
      waiting ids, "Offline only"), Cache (entries per language, "Clear cache" with inline
      confirmation), About; texts English / German (`core/I18n`), shared look from
      `libs/trailpack-ui`.
- Notes: the ABI behaviour lives in the pure `api/Service` (host-tested end to end:
      `structSize` copies, caps, ResolveMany, events); `dllmain.cpp` only binds it. WinINet and
      the Nexus binding can only be tested in game (no Windows runtime here). Unload order:
      zero the table -> UNLOADING -> stop the worker (waits for a request in flight, max. the
      30 s timeout) -> write the caches.
- **User test (in game):** load Core; options page shows "Online" and a game build after a few
      seconds; toggle "Offline only" and back; clear the cache; unload / reload Core in Nexus.
      Files: `addons/TrailpackCore/settings.json`, `cache/en|de/*.json` appear once something was
      resolved (stage 5 makes Memo ask).

### Stage 5 — Memo on Core (done; in-game test open)
- [x] `integrations/CoreClient` replaces `DecoderClient`; Decoder Ring removed everywhere (D15).
- [x] Event chips, `/e` and the event picker read from Core; Memo's `generated/EventData` and
      `tools/gen_events.py` removed.
- [x] Greyed-out features + hints without Core (D17), upgrade prefetch (D18).
- [x] No references to other creators' repositories or addons left in Memo (D21): Pie UI
      integration removed (`integrations/PieUiClient.*`, left-click on a waypoint chip copies the
      code, README / guide / AGENTS / I18n entries gone), "adapted from Pie UI" comments removed;
      check with a grep for `PieOrCake|Pie ?UI|PieUI|Decoder|Magpie|github.com` (only our own
      repo URL may remain). **Keep** the `Copyright (c) 2026 PieOrCake` line in
      `trailpack-memo/LICENSE` (MIT notice for the code derived from Magpie Memo).
- [x] README / guide / AGENTS / PLAN of Memo updated. User test with and without Core, and
      unloading Core mid-session.
- Notes: Memo reads everything through `integrations/CoreClient` (cached chip records, event list
      per Memo language rebuilt in `CoreClient::Frame()`); Memo's `events/Events` keeps only
      tokens, the `/e` trigger, time texts and `PrettyId` (event name read off the id without
      Core). The event data / schedule tests moved from Memo to Core. Memo DLL 0.97 -> 0.93 MB.
- **User test (in game):** with Core: chips show names / icons / tooltips (item with runes,
      recipe, skill, trait, waypoint, build "[Mirage Build]"), event chips + Event button + `/e`
      work, Settings > General says "Trailpack Core: connected". Without Core (disable it in
      Nexus while Memo runs): chips fall back to "[Item]" etc., Event button and "Events" settings
      greyed out with hover text, `/e` shows no list, event chips show a grey name; enable Core
      again -> everything is back without restart. Left-click a waypoint chip copies its code.

### Stage 6 — Release (workflow done; repos + first release open)
- [x] D20 research (Nexus source, `Host/Addons/Addon.cpp`): `UP_GitHub` reads
      `/repos/<owner>/<repo>/releases`, accepts only plain version tags (`1.2.3` / `v1.2.3`,
      `Core/Versioning/Version.h`), takes the highest one and its **first `.dll`** — no match on
      the addon's name. `UP_Direct` compares only an MD5 (`<url>.md5`), no versions.
      **Decision (user, 2026-10-30): one release-only repository per addon**
      (`tobia-codes/trailpack-core`, `tobia-codes/trailpack-memo`), source stays here.
- [x] `.github/workflows/release.yml`: tag `core-v<version>` / `memo-v<version>` here -> checks
      the tag against the DLL's `V_*`, host tests, MinGW build, `gh release create v<version>`
      in the release repository (secret `RELEASE_TOKEN`). Both addons: `UP_GitHub` + UpdateLink.
- [x] Changesets + release on merge to `main` (`tools/changeset.py`), release READMEs, CI — the rest of the way to
      the Nexus Addon Library (blockers, Raidcore contact, repos, token, screenshots, versions)
      is planned in [`../../plans/RELEASE.md`](../../plans/RELEASE.md).

---

## Backlog
- Trading-post prices (`/v2/commerce/prices`, memory only, ~5 min TTL).
- **Own data feed (idea, discussed 2026-10-30)** for data the API can't serve per request:
  trading-post prices of all items every ~2 h, the item search / filter index (below), later
  maybe price history. Start: a scheduled GitHub Actions workflow (e.g. every 2 h) fetches from
  the official API (same etiquette as D10) and publishes static JSON on GitHub Pages; Core downloads
  the file instead of many single requests and searches locally. Free for public repos; no server
  or database to run. Caveats: scheduled workflows pause after 60 days without repository
  activity; static files only, no queries; Pages is not meant for commercial use. A database
  (Cloudflare Workers + D1, Neon, Turso, Supabase free tiers) only once real queries are needed
  (e.g. price history) — D1's free 100k row writes per day can't take ~27k prices every 2 h row by
  row. Conditions: public API data only, never an API key or account data through it; Core falls
  back to the official API when the feed is down; extends the allowed data sources in
  `CLAUDE.md` ("Guild Wars 2 rules") — needs the user's decision and Raidcore's answer whether an
  addon reading such a feed keeps "No Special Label" (plans/RELEASE.md, Raidcore contact).
- **Account data with an optional API key.** Without a key Core behaves as today; with a key it
  also serves account data (e.g. bank, inventories, material storage, wallet, unlocks). Points to
  decide with the user before building it:
  - which endpoints in v1, and which key permissions they need (checked via `/v2/tokeninfo`,
    missing ones shown on the settings page);
  - **required:** where the key is entered (Core settings page), state exactly which permissions
    the key needs (e.g. `account`, `inventories`, `wallet`, `unlocks`), each with a short reason
    and the features it unlocks, plus a link to create a key
    (<https://account.arena.net/applications>). After entering a key, show per permission whether
    it is granted; features whose permission is missing are greyed out with a hover text naming
    the missing permission. Never ask for more permissions than the enabled features need;
  - **key handling (required, and stated to the user next to the key field and in the README):**
    - the key is stored **only locally** on the user's PC, in `addons/TrailpackCore/`, encrypted
      with Windows DPAPI (`CryptProtectData`, bound to the Windows user) so it survives restarts;
      "Remove key" deletes it;
    - it is sent **only to the official API** `https://api.guildwars2.com` over HTTPS, as an
      `Authorization: Bearer` header (not as `?access_token=` in the URL, so it never shows up in
      URLs or logs) — never to a private server, the wiki or any other host;
    - it is used **only** to read account data from the official API — nothing else;
    - it is never passed to other addons through the ABI, never written to logs, cache files or
      exports, and never shown in full in the UI (masked input, last characters only);
  - privacy: every loaded addon can query Core, so account data would be readable by any of
    them — e.g. a switch "Share account data with other addons" (default off) or a per-addon list;
  - freshness: account data changes constantly and the API caches it ~5 min: memory only, refresh
    interval, still through the token bucket (D10);
  - ABI: variable-length lists (bank slots etc.) need a paged / copy-into-buffer call (D7).
- Breakbar ("Defiance Break") enrichment for API skills from the wiki (Decoder Ring feature,
  see the test checklist).
- **Item search and filter** (by name, rarity, type, ...): the API has no search endpoint, only
  lookups by id, so this needs a built-in or downloaded index over all ~70,000 items (several MB,
  DE + EN). Decide: built into the DLL vs. generated once and cached on disk; which fields the
  index holds; ABI for paged search results (D7).

---

## Changelog of this plan
- 2026-10-30 — Plan set up with the user (decisions D1-D20, stages 1-6).
- 2026-10-30 — `ResolveMany` added to D8; backlog: account data with optional API key, item
  search / filter index. Key field must list the required permissions.
- 2026-10-30 — Account data: key handling rules (local DPAPI storage, official API only via
  header, never shared) and the matching user-facing statement.
- 2026-10-30 — Renamed Trailpack Codex -> Trailpack Core (folder, DLL, DataLink / events,
  signature `0x5450436F` "TPCo", namespace `Trailpack::Core`).
- 2026-10-30 — Stage 3: test checklist derived from Decoder Ring's host tests (own fixtures).
- 2026-10-30 — D21: Memo drops all references to other creators (Pie UI integration removed in
  stage 5); the LICENSE copyright line stays.
- 2026-10-30 — Stages 1-4 implemented (notes under each stage); breakbar enrichment moved to the
  backlog.
- 2026-10-30 — Stage 5 implemented: Memo on Core, third-party integrations removed from Memo.
- 2026-10-30 — Stage 6: release-only repo per addon decided (D20), release workflow added.
