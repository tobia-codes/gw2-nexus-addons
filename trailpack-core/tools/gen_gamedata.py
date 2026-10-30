#!/usr/bin/env python3
"""Generate src/generated/GameData.{h,cpp}: waypoints / PoIs, maps, specialisations and
professions in English and German, from the official API.

Built into the DLL on purpose (PLAN D13): there is no API endpoint per PoI id (names only live
in the continent floors, several MB each), and all of this changes only with content patches.

Sources:
  * /v2/continents/{1,2}/floors/{f}   PoI id, kind, map and name. A PoI's name is only filled on
                                      its "home" floor, so every floor is read (English); German
                                      is fetched only for the floors that named a PoI.
  * /v2/maps?ids=all                  map names
  * /v2/specializations?ids=all       specialisation names, profession, elite flag
  * /v2/professions?ids=all           profession names

Downloads are cached in tools/.cache/ and throttled (1 request per second, gzip), so a re-run
after a content patch only needs `rm -r trailpack-core/tools/.cache/api-*`.

Usage (from the repo root):  python3 trailpack-core/tools/gen_gamedata.py
Stdlib only.
"""

import gzip
import hashlib
import json
import time
import urllib.request
from pathlib import Path

ROOT       = Path(__file__).resolve().parents[1]           # trailpack-core/
CACHE      = ROOT / "tools" / ".cache"
OUT_H      = ROOT / "src" / "generated" / "GameData.h"
OUT_CPP    = ROOT / "src" / "generated" / "GameData.cpp"
API        = "https://api.guildwars2.com/v2"
USER_AGENT = "TrailpackCore-gen_gamedata/1.0 (+https://github.com/tobia-codes/gw2-nexus-addons)"
CONTINENTS = [1, 2]
# Profession codes of build chat links (fixed by the link format) -> API profession id.
PROFESSIONS = ["Guardian", "Warrior", "Engineer", "Ranger", "Thief",
               "Elementalist", "Mesmer", "Necromancer", "Revenant"]
POI_KINDS  = {"waypoint": 0, "landmark": 1, "vista": 2, "unlock": 1}

_last_request = 0.0


def get(path: str):
    """JSON of an API path (cached; at most one request per second)."""
    global _last_request
    CACHE.mkdir(parents=True, exist_ok=True)
    cached = CACHE / ("api-" + hashlib.sha1(path.encode()).hexdigest())
    if cached.exists():
        return json.loads(cached.read_text(encoding="utf-8"))
    wait = 1.0 - (time.time() - _last_request)
    if wait > 0:
        time.sleep(wait)
    req = urllib.request.Request(API + path, headers={"User-Agent": USER_AGENT,
                                                       "Accept-Encoding": "gzip"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                raw = r.read()
            break
        except urllib.error.HTTPError as e:
            if e.code == 404:
                _last_request = time.time()
                return None
            if e.code in (429, 500, 502, 503, 504) and attempt < 3:
                time.sleep(10 * (attempt + 1))
                continue
            raise
    _last_request = time.time()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    text = raw.decode("utf-8")
    cached.write_text(text, encoding="utf-8")
    return json.loads(text)


def floor_pois(continent: int, floor: int, lang: str):
    """{poi id: (name, map id, kind)} of one floor (named PoIs only)."""
    data = get(f"/continents/{continent}/floors/{floor}?lang={lang}")
    out = {}
    for region in (data or {}).get("regions", {}).values():
        for map_id, m in region.get("maps", {}).items():
            for p in m.get("points_of_interest", {}).values():
                name = (p.get("name") or "").strip()
                kind = POI_KINDS.get(p.get("type"))
                if name and kind is not None:
                    out.setdefault(p["id"], (name, int(map_id), kind))
    return out


def c_str(s) -> str:
    if s is None:
        return "nullptr"
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    # ── PoIs: English from every floor, German from the floors that named one ──
    pois, home = {}, {}
    for c in CONTINENTS:
        for f in get(f"/continents/{c}/floors"):
            for pid, val in floor_pois(c, f, "en").items():
                if pid not in pois:
                    pois[pid], home[pid] = val, (c, f)
        print(f"continent {c}: {len(pois)} named PoIs so far")
    names_de = {}
    for c, f in sorted(set(home.values())):
        for pid, (name, _, _) in floor_pois(c, f, "de").items():
            if home.get(pid) == (c, f):
                names_de[pid] = name

    # ── Maps, specialisations, professions ──
    maps_en = {m["id"]: m["name"] for m in get("/maps?ids=all&lang=en")}
    maps_de = {m["id"]: m["name"] for m in get("/maps?ids=all&lang=de")}
    specs_en = get("/specializations?ids=all&lang=en")
    specs_de = {s["id"]: s["name"] for s in get("/specializations?ids=all&lang=de")}
    prof_de = {p["id"]: p["name"] for p in get("/professions?ids=all&lang=de")}

    used_maps = sorted({m for _, m, _ in pois.values()})
    map_index = {m: i for i, m in enumerate(used_maps)}

    def de(en, de_name):
        return None if not de_name or de_name == en else de_name

    h = """#pragma once
// GameData.h — GENERATED by tools/gen_gamedata.py; do not edit. PURE: no ImGui/Nexus/Win32.
//
// Built-in game data (PLAN D13): waypoints / PoIs with their map, specialisations and
// professions, English and German. German nullptr = same as English. Arrays are sorted by id.

#include <cstddef>
#include <cstdint>

namespace Trailpack { namespace Core { namespace GameData {

enum PoiKind : uint8_t { POI_WAYPOINT = 0, POI_LANDMARK = 1, POI_VISTA = 2 };

struct MapDef {
    uint32_t    id;
    const char* nameEn;
    const char* nameDe;
};

struct PoiDef {
    uint32_t    id;          // chat-link PoI id (0x04 links)
    uint16_t    map;         // index into kMaps
    uint8_t     kind;        // PoiKind
    const char* nameEn;
    const char* nameDe;
};

struct SpecDef {
    uint16_t    id;          // chat-link specialisation id (0x0D build links)
    uint8_t     profession;  // build-link profession code 1..9
    uint8_t     elite;       // 1 = elite specialisation
    const char* nameEn;
    const char* nameDe;
};

struct ProfessionDef {
    const char* nameEn;      // index = build-link profession code - 1
    const char* nameDe;
};

extern const MapDef        kMaps[];
extern const size_t        kMapCount;
extern const PoiDef        kPois[];
extern const size_t        kPoiCount;
extern const SpecDef       kSpecs[];
extern const size_t        kSpecCount;
extern const ProfessionDef kProfessions[];
extern const size_t        kProfessionCount;

}}} // namespace Trailpack::Core::GameData
"""
    lines = ["// GameData.cpp — GENERATED by tools/gen_gamedata.py; do not edit (see GameData.h).",
             '#include "GameData.h"', "", "namespace Trailpack { namespace Core { namespace GameData {", "",
             "const MapDef kMaps[] = {"]
    for m in used_maps:
        en = maps_en.get(m, "")
        lines.append(f"    {{ {m}, {c_str(en)}, {c_str(de(en, maps_de.get(m)))} }},")
    lines += ["};", "const size_t kMapCount = sizeof kMaps / sizeof kMaps[0];", "",
              "const PoiDef kPois[] = {"]
    for pid in sorted(pois):
        name, m, kind = pois[pid]
        lines.append(f"    {{ {pid}, {map_index[m]}, {kind}, {c_str(name)}, {c_str(de(name, names_de.get(pid)))} }},")
    lines += ["};", "const size_t kPoiCount = sizeof kPois / sizeof kPois[0];", "",
              "const SpecDef kSpecs[] = {"]
    for s in sorted(specs_en, key=lambda s: s["id"]):
        code = PROFESSIONS.index(s["profession"]) + 1
        lines.append(f"    {{ {s['id']}, {code}, {1 if s.get('elite') else 0}, {c_str(s['name'])}, "
                     f"{c_str(de(s['name'], specs_de.get(s['id'])))} }},")
    lines += ["};", "const size_t kSpecCount = sizeof kSpecs / sizeof kSpecs[0];", "",
              "const ProfessionDef kProfessions[] = {"]
    for p in PROFESSIONS:
        lines.append(f"    {{ {c_str(p)}, {c_str(de(p, prof_de.get(p)))} }},")
    lines += ["};", "const size_t kProfessionCount = sizeof kProfessions / sizeof kProfessions[0];", "",
              "}}} // namespace Trailpack::Core::GameData", ""]

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(h, encoding="utf-8")
    OUT_CPP.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT_H.name} + {OUT_CPP.name}: {len(pois)} PoIs ({len(names_de)} with German name), "
          f"{len(used_maps)} maps, {len(specs_en)} specialisations")


if __name__ == "__main__":
    main()
