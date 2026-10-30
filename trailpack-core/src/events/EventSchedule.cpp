// EventSchedule.cpp — see EventSchedule.h. Schedule maths and search moved here from
// Trailpack Memo's events/Events.cpp (PLAN D14).
#include "events/EventSchedule.h"

namespace Trailpack { namespace Core { namespace Events {

static const int64_t kDay = 24 * 60 * 60;

// Floor division (correct for negative values, too).
static int64_t floorDiv(int64_t a, int64_t b)
{
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}

const EventDef* Find(const std::string& id)
{
    for (size_t i = 0; i < kEventCount; ++i)
        if (id == kEvents[i].id) return &kEvents[i];
    return nullptr;
}

Status State(const EventDef& e, int64_t now)
{
    // Every event repeats daily from 00:00 UTC. An occurrence may run past midnight, so the
    // running one can belong to yesterday; the next start is today's or tomorrow's.
    const int64_t today = floorDiv(now, kDay) * kDay;
    Status st;
    bool haveNext = false;
    for (int64_t day = today - kDay; day <= today + kDay; day += kDay) {
        for (int k = 0; k < e.count; ++k) {
            const int64_t s = day + (int64_t)e.starts[k] * 60;
            const int64_t end = s + (int64_t)e.durations[k] * 60;
            if (s <= now && now < end) {
                st.active = true;
                st.start  = s;
                st.end    = end;
                st.place  = e.placeOf[k];
                return st;
            }
            if (s > now && !haveNext) {
                haveNext = true;
                st.start = s;
                st.end   = end;
                st.place = e.placeOf[k];
            }
        }
    }
    return st;
}

std::vector<Occurrence> NextOccurrences(const EventDef& e, int64_t now, int n)
{
    std::vector<Occurrence> out;
    if (e.count == 0 || n <= 0) return out;
    for (int64_t day = floorDiv(now, kDay) * kDay; (int)out.size() < n; day += kDay) {
        for (int k = 0; k < e.count && (int)out.size() < n; ++k) {
            Occurrence o;
            o.start = day + (int64_t)e.starts[k] * 60;
            o.end   = o.start + (int64_t)e.durations[k] * 60;
            o.place = e.placeOf[k];
            if (o.start > now) out.push_back(o);
        }
    }
    return out;
}

const char* Name(const EventDef& e, Lang lang)
{
    return lang == Lang::De && e.nameDe != nullptr ? e.nameDe : e.nameEn;
}

const char* MapName(const PlaceDef& p, Lang lang)
{
    const char* m = lang == Lang::De && p.mapDe != nullptr ? p.mapDe : p.mapEn;
    return m != nullptr ? m : "";
}

const char* WaypointName(const PlaceDef& p, Lang lang)
{
    const char* w = lang == Lang::De && p.waypointNameDe != nullptr ? p.waypointNameDe : p.waypointNameEn;
    return w != nullptr ? w : "";
}

std::string Maps(const EventDef& e, Lang lang)
{
    std::string out;
    for (int i = 0; i < e.placeCount; ++i) {
        const std::string m = MapName(e.places[i], lang);
        if (m.empty() || (", " + out + ", ").find(", " + m + ", ") != std::string::npos) continue;
        if (!out.empty()) out += ", ";
        out += m;
    }
    return out;
}

const char* CategoryName(uint8_t category, Lang lang)
{
    if (category >= kCategoryCount) return "";
    const CategoryDef& c = kCategories[category];
    return lang == Lang::De && c.nameDe != nullptr ? c.nameDe : c.nameEn;
}

const char* CategoryShort(uint8_t category, Lang lang)
{
    if (category >= kCategoryCount) return "";
    // By the generated English name, so a new category simply shows its full name.
    struct Short { const char* nameEn; const char* en; const char* de; };
    static const Short kShort[] = {
        { "Core Tyria",             "Core",     "Zentral" },
        { "Living World Season 2",  "LW2",      "LW2" },
        { "Heart of Thorns",        "HoT",      "HoT" },
        { "Living World Season 3",  "LW3",      "LW3" },
        { "Path of Fire",           "PoF",      "PoF" },
        { "Living World Season 4",  "LW4",      "LW4" },
        { "The Icebrood Saga",      "IBS",      "IBS" },
        { "End of Dragons",         "EoD",      "EoD" },
        { "Secrets of the Obscure", "SotO",     "SotO" },
        { "Janthir Wilds",          "JW",       "JW" },
        { "Visions of Eternity",    "VoE",      "VoE" },
        { "Public Instances",       "Instance", "Instanz" },
        { "Special Events",         "Festival", "Festival" },
    };
    const char* en = kCategories[category].nameEn;
    for (const Short& s : kShort)
        if (std::string(s.nameEn) == en) return lang == Lang::De ? s.de : s.en;
    return CategoryName(category, lang);
}

// Lower-case for matching: ASCII and the Latin-1 letters as UTF-8 (C3 80..9E -> C3 A0..BE,
// e.g. "Ä" -> "ä"), so German names match whatever case the user types.
static std::string folded(const char* s)
{
    std::string out;
    if (s == nullptr) return out;
    for (const char* p = s; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            out += (char)(c + 32);
        } else if (c == 0xC3 && p[1] != '\0' && (unsigned char)p[1] >= 0x80 &&
                   (unsigned char)p[1] <= 0x9E && (unsigned char)p[1] != 0x97) {   // not "×"
            out += (char)c;
            out += (char)((unsigned char)p[1] + 0x20);
            ++p;
        } else {
            out += (char)c;
        }
    }
    return out;
}

std::vector<const EventDef*> Search(const std::string& query)
{
    const std::string q = folded(query.c_str());
    std::vector<const EventDef*> out;
    for (size_t i = 0; i < kEventCount; ++i) {
        const EventDef& e = kEvents[i];
        std::vector<const char*> fields = { e.nameEn, e.nameDe, e.id };
        for (int p = 0; p < e.placeCount; ++p) {
            fields.push_back(e.places[p].mapEn);
            fields.push_back(e.places[p].mapDe);
        }
        for (const char* f : fields) {
            if (q.empty() || (f != nullptr && folded(f).find(q) != std::string::npos)) {
                out.push_back(&e);
                break;
            }
        }
    }
    return out;
}

}}} // namespace Trailpack::Core::Events
