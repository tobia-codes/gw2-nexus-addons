#!/usr/bin/env python3
"""Generate src/generated/EventData.{h,cpp} from the GW2 wiki event timer data.

Sources (see plans/EVENTS.md, D2):
  * English wiki "Widget:Event timer/data.json" -- schedules, categories, names, waypoint
    chat codes. Default: the live wiki; --en-data takes a URL or file (e.g. a Wayback Machine
    snapshot while the wiki is down).
  * German wiki "Widget:Event-Timer/script.js" (optional, --de-script) -- German event and meta
    names, matched to the English events by waypoint chat code.
  * Official API /v2/continents/1/floors -- waypoint and map names in English and German.
Manual fixes: tools/event_overrides.json (D3). Downloads are cached in tools/.cache/.

Usage (from the repo root):
  python3 trailpack-memo/tools/gen_events.py [--en-data URL|FILE] [--de-script URL|FILE]

Stdlib only. Prints a summary (D4) after writing the files.
"""

import argparse
import base64
import gzip
import hashlib
import html
import json
import re
import struct
import sys
import urllib.request
from pathlib import Path

ROOT       = Path(__file__).resolve().parents[1]           # trailpack-memo/
OVERRIDES  = ROOT / "tools" / "event_overrides.json"
CACHE      = ROOT / "tools" / ".cache"
OUT_H      = ROOT / "src" / "generated" / "EventData.h"
OUT_CPP    = ROOT / "src" / "generated" / "EventData.cpp"

EN_DATA_URL   = "https://wiki.guildwars2.com/index.php?title=Widget:Event_timer/data.json&action=raw"
DE_SCRIPT_URL = "https://wiki-de.guildwars2.com/index.php?title=Widget:Event-Timer/script.js&action=raw"
API           = "https://api.guildwars2.com/v2/continents/1/floors"
DAY           = 1440
USER_AGENT    = "TrailpackMemo-gen_events/1.0 (+https://github.com/tobia-codes/gw2-nexus-addons)"


# ── Fetching (cached) ───────────────────────────────────────────────────────

def fetch(src: str) -> str:
    """Text of a URL (cached by URL) or a local file."""
    if not src.startswith(("http://", "https://")):
        return Path(src).read_text(encoding="utf-8")
    CACHE.mkdir(parents=True, exist_ok=True)
    cached = CACHE / hashlib.sha1(src.encode()).hexdigest()
    if cached.exists():
        return cached.read_text(encoding="utf-8")
    req = urllib.request.Request(src, headers={"User-Agent": USER_AGENT, "Accept-Encoding": "identity"})
    with urllib.request.urlopen(req, timeout=120) as r:
        raw = r.read()
    if raw[:2] == b"\x1f\x8b":   # the Wayback Machine may serve gzip regardless
        raw = gzip.decompress(raw)
    text = raw.decode("utf-8")
    if "undergoing maintenance" in text[:3000]:
        sys.exit(f"error: {src} returned the wiki maintenance page; pass an archived copy instead")
    cached.write_text(text, encoding="utf-8")
    return text


# ── Wiki data ───────────────────────────────────────────────────────────────

def day_timeline(meta: dict, alias: dict) -> list:
    """[(segment key, start, end)] for one UTC day: `partial` first, then `pattern` repeated
    (the widget's rule). Back-to-back runs of the same segment stay separate occurrences, except
    an aliased segment (a "(continued)" piece), which joins the one before it. A segment cut at
    midnight is joined with the one that opens the day, so it starts before 00:00 and runs past
    24:00."""
    seq = meta["sequences"]
    parts = list(seq.get("partial", []))
    pattern = list(seq.get("pattern", []))
    total = sum(p["d"] for p in parts)
    if total < DAY and sum(p["d"] for p in pattern) > 0:
        while total < DAY:
            parts += pattern
            total += sum(p["d"] for p in pattern)
    out, t = [], 0
    for p in parts:
        if t >= DAY:
            break
        aliased = str(p["r"]) in alias
        r = alias.get(str(p["r"]), str(p["r"]))
        end = min(t + p["d"], DAY)
        if aliased and out and out[-1][0] == r and out[-1][2] == t:
            out[-1] = (r, out[-1][1], end)
        else:
            out.append((r, t, end))
        t += p["d"]
    # Join across midnight: the last segment (cut at 24:00) continues as the first one.
    if len(out) > 1 and out[0][1] == 0 and out[-1][2] == DAY and out[0][0] == out[-1][0]:
        r, s, e = out[-1]
        out[-1] = (r, s, e + out[0][2])
        out = out[1:]
    return out


def parse_de_script(text: str) -> tuple:
    """German widget (JS object literal, served as a wiki page): ([meta name], [(meta index,
    segment name, chat code)])."""
    text = html.unescape(re.sub(r"<[^>]+>", "", text))
    metas, segs = [], []
    for m in re.finditer(r'category:\s*"([^"]*)",\s*name:\s*"([^"]*)"|'
                         r'\d+:\s*\{\s*name:\s*"([^"]*)"([^}]*)\}', text):
        if m.group(2) is not None:
            metas.append(m.group(2))
        elif metas and m.group(3):
            cl = re.search(r'chatlink:\s*"([^"]*)"', m.group(4))
            segs.append((len(metas) - 1, m.group(3), cl.group(1).strip() if cl else ""))
    return metas, segs


# ── API: waypoint + map names ───────────────────────────────────────────────

def poi_id(chat: str) -> int:
    raw = base64.b64decode(chat.strip()[2:-1])
    return struct.unpack("<I", raw[1:5])[0] if raw and raw[0] == 0x04 else -1


def poi_names(wanted: set) -> dict:
    """POI id -> {en, de, map_en, map_de}, searching floor 1 first, then the others."""
    floors = json.loads(fetch(API))
    floors = [1] + [f for f in floors if f != 1]
    found = {}
    for f in floors:
        if wanted <= set(found):
            break
        per_lang = {}
        for lang in ("en", "de"):
            data = json.loads(fetch(f"{API}/{f}?lang={lang}"))
            for region in data.get("regions", {}).values():
                for m in region.get("maps", {}).values():
                    for p in m.get("points_of_interest", {}).values():
                        if p["id"] in wanted and p["id"] not in found:
                            per_lang.setdefault(p["id"], {})[lang] = (p.get("name", ""), m["name"])
        for pid, v in per_lang.items():
            if "en" in v:
                found[pid] = {"en": v["en"][0], "map_en": v["en"][1],
                              "de": v.get("de", ("", ""))[0], "map_de": v.get("de", ("", ""))[1]}
    return found


# ── Helpers ─────────────────────────────────────────────────────────────────

def slug(s: str) -> str:
    s = s.lower().replace("'", "")
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")


def fmt(template, values: dict):
    """Fill {placeholders}; None if one of them has no value (e.g. no German name)."""
    if template is None:
        return None
    for key in re.findall(r"\{(\w+)\}", template):
        if not values.get(key):
            return None
    return template.format(**values)


def c_str(s) -> str:
    if s is None:
        return "nullptr"
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ── Main ────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--en-data", default=EN_DATA_URL, help="English data.json (URL or file)")
    ap.add_argument("--de-script", default=DE_SCRIPT_URL, help="German widget script (URL or file); '' to skip")
    args = ap.parse_args()

    ov = json.loads(OVERRIDES.read_text(encoding="utf-8"))
    data = json.loads(fetch(args.en_data))
    de_metas, de_segs = parse_de_script(fetch(args.de_script)) if args.de_script else ([], [])

    # German segment names by unique chat code; German meta names by the meta sharing most codes.
    de_by_chat = {}
    for mi, name, cl in de_segs:
        if cl:
            de_by_chat.setdefault(cl, set()).add((mi, name))
    en_chat_count = {}
    for meta in data["events"].values():
        for seg in meta["segments"].values():
            cl = (seg.get("chatlink") or "").strip()
            if cl:
                en_chat_count[cl] = en_chat_count.get(cl, 0) + 1

    # Collect events: one per segment, or one per meta for "merge" metas (the same event moving
    # between places, e.g. the Ley-Line Anomaly). Each part = one segment with its place.
    events, wanted = [], set()
    for key, meta in data["events"].items():
        if key in ov["exclude_metas"]:
            continue
        mo = ov["metas"].get(key, {})
        alias = mo.get("alias", {})
        timeline = day_timeline(meta, alias)
        codes = [(s.get("chatlink") or "").strip() for s in meta["segments"].values()]
        votes = {}
        for cl in codes:
            for mi, _ in de_by_chat.get(cl, ()):
                votes[mi] = votes.get(mi, 0) + 1
        meta_de = de_metas[max(votes, key=votes.get)] if votes else None
        parts = []
        for sk, seg in meta["segments"].items():
            if not seg.get("name") or sk in alias or sk in mo.get("exclude_segments", []):
                continue
            occ = [(s, e - s) for r, s, e in timeline if r == sk]
            if not occ:
                continue
            cl = (seg.get("chatlink") or "").strip()
            de = de_by_chat.get(cl, set())
            seg_de = next(iter(de))[1] if len(de) == 1 and en_chat_count.get(cl) == 1 else None
            if cl:
                wanted.add(poi_id(cl))
            parts.append({"segment": seg["name"], "segment_de": seg_de, "chat": cl,
                          "wiki": seg.get("link") or seg["name"], "occ": occ})
        groups = [parts] if mo.get("merge") and parts else [[p] for p in parts]
        for g in groups:
            events.append({"meta_name": meta["name"], "meta_de": meta_de, "category": meta["category"],
                           "parts": g, "mo": mo})

    pois = poi_names(wanted)

    # Names, places, ids, overrides.
    out, ids = [], set()
    for ev in events:
        mo, first = ev["mo"], ev["parts"][0]
        places, occ = [], []
        for part in ev["parts"]:
            p = pois.get(poi_id(part["chat"])) if part["chat"] else None
            place = {"map": mo.get("map", p["map_en"] if p else "") or None,
                     "map_de": mo.get("map_de", p["map_de"] if p else "") or None,
                     "waypoint": part["chat"] or None,
                     "wp": p["en"] if p else None, "wp_de": p["de"] if p and p["de"] else None}
            if place not in places:
                places.append(place)
            occ += [(s, d, places.index(place)) for s, d in part["occ"]]
        values = {"segment": first["segment"], "segment_de": first["segment_de"],
                  "meta": ev["meta_name"], "meta_de": ev["meta_de"],
                  "map": places[0]["map"], "map_de": places[0]["map_de"]}
        name = fmt(mo.get("name", "{segment}"), values) or first["segment"]
        name_de = fmt(mo.get("name_de", "{segment_de}"), values)
        rec = {"id": slug(name), "name": name, "name_de": name_de, "category": ev["category"],
               "places": places, "wiki": mo.get("wiki", first["wiki"]), "occ": sorted(occ)}
        eo = ov["events"].get(rec["id"], {})
        if eo.get("exclude"):
            continue
        for k in ("name", "name_de"):
            if k in eo:
                rec[k] = eo[k]
        for k in ("map", "map_de", "waypoint"):   # single-place fixes
            if k in eo:
                rec["places"][0][k] = eo[k]
        rec["id"] = eo.get("id", rec["id"])
        if rec["id"] in ids:
            sys.exit(f"error: duplicate event id '{rec['id']}' -- set an id in event_overrides.json")
        ids.add(rec["id"])
        out.append(rec)

    categories = []
    for rec in out:
        if rec["category"] not in categories:
            categories.append(rec["category"])

    write_files(out, categories, ov.get("categories_de", {}), args)
    summary(out, categories)


def write_files(events, categories, cat_de, args) -> None:
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    header = f"""#pragma once
// EventData.h — GENERATED by tools/gen_events.py; do not edit. Fix data in
// tools/event_overrides.json and re-run (see plans/EVENTS.md). PURE: no ImGui/Nexus/Win32.
//
// Every event repeats daily: `starts` are minutes after 00:00 UTC (0..1439); an occurrence
// may run past midnight (start + duration > 1440). An event happens at one or more places
// (map + waypoint); `placeOf` says where each occurrence is (rotating events like the
// Ley-Line Anomaly). German texts nullptr = use English.

#include <cstddef>
#include <cstdint>

namespace Trailpack {{ namespace Events {{

constexpr int kDayMinutes = {DAY};

struct CategoryDef {{
    const char* nameEn;
    const char* nameDe;          // nullptr: expansion name, not translated
}};

struct PlaceDef {{
    const char* mapEn;           // nullptr: none known
    const char* mapDe;
    const char* waypoint;        // chat code "[&B...]", nullptr: none
    const char* waypointNameEn;
    const char* waypointNameDe;
}};

struct EventDef {{
    const char*     id;          // stable English slug, used in {{{{event:<id>}}}}
    const char*     nameEn;
    const char*     nameDe;
    uint8_t         category;    // index into kCategories
    const char*     wikiPage;    // English wiki page (may contain "#section")
    uint8_t         placeCount;  // >= 1
    const PlaceDef* places;
    uint8_t         count;       // occurrences per day
    const uint16_t* starts;      // minutes after 00:00 UTC, ascending
    const uint16_t* durations;   // minutes, parallel to starts
    const uint8_t*  placeOf;     // index into places, parallel to starts
}};

extern const CategoryDef kCategories[];
extern const size_t      kCategoryCount;
extern const EventDef    kEvents[];
extern const size_t      kEventCount;

}}}} // namespace Trailpack::Events
"""
    lines = ["// EventData.cpp — GENERATED by tools/gen_events.py; do not edit (see EventData.h).",
             f"// Source: {args.en_data}",
             '#include "EventData.h"', "", "namespace Trailpack { namespace Events {", ""]
    for i, ev in enumerate(events):
        lines.append(f"static const uint16_t kS{i}[] = {{ {', '.join(str(s) for s, _, _ in ev['occ'])} }};")
        lines.append(f"static const uint16_t kD{i}[] = {{ {', '.join(str(d) for _, d, _ in ev['occ'])} }};")
        lines.append(f"static const uint8_t  kP{i}[] = {{ {', '.join(str(p) for _, _, p in ev['occ'])} }};")
        lines.append(f"static const PlaceDef kL{i}[] = {{")
        for pl in ev["places"]:
            lines.append(f"    {{ {c_str(pl['map'])}, {c_str(pl['map_de'])}, {c_str(pl['waypoint'])}, "
                         f"{c_str(pl['wp'])}, {c_str(pl['wp_de'])} }},")
        lines.append("};")
    lines += ["", "const CategoryDef kCategories[] = {"]
    for c in categories:
        lines.append(f"    {{ {c_str(c)}, {c_str(cat_de.get(c))} }},")
    lines += ["};", "const size_t kCategoryCount = sizeof kCategories / sizeof kCategories[0];", "",
              "const EventDef kEvents[] = {"]
    for i, ev in enumerate(events):
        lines.append(f"    {{ {c_str(ev['id'])}, {c_str(ev['name'])}, {c_str(ev['name_de'])}, "
                     f"{categories.index(ev['category'])},")
        lines.append(f"      {c_str(ev['wiki'])}, {len(ev['places'])}, kL{i}, "
                     f"{len(ev['occ'])}, kS{i}, kD{i}, kP{i} }},")
    lines += ["};", "const size_t kEventCount = sizeof kEvents / sizeof kEvents[0];", "",
              "}} // namespace Trailpack::Events", ""]
    OUT_H.write_text(header, encoding="utf-8")
    OUT_CPP.write_text("\n".join(lines), encoding="utf-8")


def summary(events, categories) -> None:
    print(f"wrote {OUT_H.relative_to(ROOT)} + {OUT_CPP.name}: {len(events)} events, {len(categories)} categories")
    for c in categories:
        print(f"  {sum(e['category'] == c for e in events):3}  {c}")
    multi = [f"{e['id']} ({len(e['places'])})" for e in events if len(e["places"]) > 1]
    no_wp = [e["id"] for e in events if not any(p["waypoint"] for p in e["places"])]
    no_wp_name = [e["id"] for e in events if any(p["waypoint"] and not p["wp"] for p in e["places"])]
    no_de = [e["id"] for e in events if not e["name_de"]]
    print(f"rotating between places ({len(multi)}): {', '.join(multi) or '-'}")
    print(f"without waypoint ({len(no_wp)}): {', '.join(no_wp) or '-'}")
    print(f"waypoint not found in the API ({len(no_wp_name)}): {', '.join(no_wp_name) or '-'}")
    print(f"without German name ({len(no_de)}): {', '.join(no_de) or '-'}")


if __name__ == "__main__":
    main()
