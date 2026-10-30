# Trailpack Core — Working Plan

> Living plan: decisions, stages and backlog. The rules (build, identity, code rules, API
> etiquette) are in [`../AGENTS.md`](../AGENTS.md). Work the stages in order, tick boxes
> (`- [x]`) as tasks land, and update "Current state" when the architecture changes.

---

## Current state (as of 2026-10-30)

Planned, no code yet. Decisions D1-D20 were agreed with the user on 2026-10-30 — ask before
changing any of them.

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
- **D4 — Required by every Trailpack addon** and their single data source:
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
- **D20 — Update channel:** research how Nexus' GitHub update provider picks a release / asset in
  a monorepo before stage 6, then let the user choose (Memo backlog B2 covers the same question).

---

## Stages

Each stage: feature branch, Definition of Done from `AGENTS.md`, then tell the user what to verify
in game.

### Stage 1 — `libs/trailpack-chatlinks/`
- [ ] New static library: own chat-link codec (decode / encode / classify item, skin, skill,
      trait, map, recipe, build / AE2, wardrobe; segment text into link spans). No specialisation
      names (D16). Host tests.
- [ ] Root `CMakeLists.txt` adds `libs/`; Memo links the library, `src/vendor/chat/` is removed,
      Memo's tests stay green. Update Memo's AGENTS.md (vendored-codec rule) and PLAN.

### Stage 2 — `libs/trailpack-ui/`
- [ ] Extract the shared style helpers from Memo's `ui/Theme` (palette, rounding, plain window
      style) that the Core settings page needs; add the disabled-item helper (D17).
- [ ] Memo uses the library; no visual change.

### Stage 3 — Core: pure part
- [ ] `trailpack-core/CMakeLists.txt` (DLL + host tests), `add_subdirectory` in the root.
- [ ] Generators + data: events (moved from Memo incl. overrides and `plans/EVENTS.md` data
      notes), waypoints / PoIs DE + EN, specialisation names DE + EN.
- [ ] Event schedule + search (from Memo's `events/Events`, without the editor parts).
- [ ] API parsers (item, skin, skill + wiki fallback, trait, recipe) into record fields; fact
      formatting.
- [ ] Cache model (D11), batch planner, token bucket + backoff (D10) — all host-tested with an
      injected fetch.
- [ ] Every case of the test checklist below has a host test.

#### Test checklist (from Decoder Ring's host tests)
Cases Decoder Ring (`PieOrCake/decoder_ring`, `tests/test_main.cpp`) learned the hard way. Our
tests cover the same cases with **our own** fixtures: capture fresh, untrimmed responses from the
API / wiki (EN and DE) into `tests/fixtures/`, never copy Decoder Ring's test code (D2). Cases
marked *(DR feature)* only apply once we build that feature.

**ABI / offline data**
- [ ] Records and API table are POD / trivially copyable; layout pinned (`structSize`, offsets).
- [ ] Build link -> spec label ("Mirage Build" for spec 59; core profession -> "Mesmer Build").
- [ ] Waypoint / PoI ids resolve from the built-in table with name, map and kind
      (waypoint / point of interest / vista), DE and EN.

**Items (`/v2/items`)**
- [ ] Armour (e.g. Zojja's Breastplate): defense, attributes incl. `CritDamage` -> "Ferocity",
      infusion slot, weight, subtype, required level, flavour text, bound on acquire.
- [ ] Weapon (e.g. Judgment Trident): weapon strength line, **no** defense line; flavour markup
      (`<c=@flavor>`, `<br>`) stripped cleanly.
- [ ] Rune (e.g. Superior Rune of the Scholar): `details.bonuses` listed verbatim (the consumer
      shows them for the upgrade ids of the parent item, D18).
- [ ] Mystic Coin: `NoSell` must **not** make it untradeable (it is traded on the TP).
- [ ] Every rarity tier maps; unknown / missing rarity stays "unknown", never a real tier.
- [ ] Subtype wording: `HelmAquatic` -> "Aquatic Helm", generic `*Aquatic` rule, `Default` is
      dropped, real slots (`Coat`, `Rifle`, `Trident`, `Speargun`) pass through.
- [ ] German response: our own labels are translated, names from the API used verbatim; the
      request URL carries `lang`.
- [ ] Tooltip lines are capped (fact count and text length) without cutting UTF-8 characters.

**Skills (`/v2/skills` + wiki fallback)**
- [ ] Normal skill: name, icon, description, facts.
- [ ] Skill missing from `/v2/skills` (mount / siege turtle / transform): wiki fallback via the
      SMW `action=ask` API — e.g. 63475 "Slam" with description and facts ("Damage", condition
      with duration, "Number of Targets", "Defiance Break", "Range"); wiki facts carry no icons.
- [ ] Name from the canonical name, not the page title (no "(turtle)" / "(effect)" suffix).
- [ ] Id collision: 63440 is also an item ("Defender's Staff"); the query must be limited to the
      skill context so it resolves to "Open Access".
- [ ] Wiki markup cleanup: bold, HTML entities, `[[Category:...]]` must not leak into facts
      (e.g. "FireCategory:Fire field skills").
- [ ] Empty wiki result (`[]`) -> failed, plain `[Skill]` label, never an invented name.
- [ ] Effect / buff links (food, guild / WvW boosts) are 0x06 links but neither in `/v2/skills`
      nor in the skill wiki space: second fallback under the effect context — e.g. 46587
      "Malnourished", 35126 "Guild WvW Reward Track Boost".
- [ ] *(DR feature)* Breakbar enrichment from the wiki: 6154 Overcharged Shot gets
      "Defiance Break: 232" (the wiki text has a stray "332" from a "sic" note — read 232);
      5492 Fire Attunement gets none, never "Defiance Break: 0"; no double line, no extra
      fetch when the fact already exists; label translated, number from the English wiki.

**Traits, recipes, skins**
- [ ] Trait 214 "Raging Storm": description, icon, facts "Recharge: 8s" (`value`),
      "Radius: 360" (falls back to `distance`), "Number of Targets: 5", "Fury (4s)" (buff with
      duration); API facts carry icons.
- [ ] Recipe 9508: output item 62905 "Elegant Huntsman's Backpack", ingredients + required
      rating; output and ingredients fetched in **one** `/v2/items?ids=` batch.
- [ ] Guild recipe 11777 (output 77721 "Silver Tequatl Trophy", rating 350): guild-upgrade
      ingredients via `/v2/guild/upgrades?ids=` (best effort).
- [ ] Recipe name localized ("Rezept: ..." in German).
- [ ] Skin: name, icon; URL carries `lang`.

**Cache, language, lifetime**
- [ ] Cache round-trip per type; an old cache format is refetched, never poisons the cache;
      a missing new field reads back as "unknown".
- [ ] Language switch never serves the warm English entry for a German request (cache
      namespaced by language).
- [ ] Nexus language -> API language mapping (anything but DE -> EN, D12).
- [ ] Invalid id (e.g. 0) fails synchronously, no request.
- [ ] **Every request ends in exactly one terminal outcome** (sync answer or event): resolved,
      failed after timeout, invalid, in cooldown — none stays "not ready" forever.
- [ ] Failure cooldown: a failed id is not refetched before the cooldown ends.
- [ ] Teardown while a fetch is in flight waits for it and emits nothing afterwards; calls after
      teardown are safe.

### Stage 4 — Core DLL
- [ ] `public/TrailpackCoreApi.h` (D5-D8, incl. `ResolveMany`) with layout tests.
- [ ] WinINet fetch, worker thread, disk cache via `SafeFile`, `/v2/build` check.
- [ ] Nexus lifecycle: DataLink, READY / UNLOADING / PING, RESOLVED from the main-thread pump,
      Nexus language -> DE / EN.
- [ ] Settings page (D3).

### Stage 5 — Memo on Core
- [ ] `integrations/CoreClient` replaces `DecoderClient`; Decoder Ring removed everywhere (D15).
- [ ] Event chips, `/e` and the event picker read from Core; Memo's `generated/EventData` and
      `tools/gen_events.py` removed.
- [ ] Greyed-out features + hints without Core (D17), upgrade prefetch (D18).
- [ ] No references to other creators' repositories or addons left in Memo (D21): Pie UI
      integration removed (`integrations/PieUiClient.*`, left-click on a waypoint chip copies the
      code, README / guide / AGENTS / I18n entries gone), "adapted from Pie UI" comments removed;
      check with a grep for `PieOrCake|Pie ?UI|PieUI|Decoder|Magpie|github.com` (only our own
      repo URL may remain). **Keep** the `Copyright (c) 2026 PieOrCake` line in
      `trailpack-memo/LICENSE` (MIT notice for the code derived from Magpie Memo).
- [ ] README / guide / AGENTS / PLAN of Memo updated. User test with and without Core, and
      unloading Core mid-session.

### Stage 6 — Release
- [ ] D20 research + user decision, then a release workflow for `TrailpackCore.dll`
      (e.g. tag prefix `core-v*`).

---

## Backlog
- Trading-post prices (`/v2/commerce/prices`, memory only, ~5 min TTL).
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
