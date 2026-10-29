// Events.cpp — see Events.h.
#include "Events.h"

#include <cstdio>

namespace Magpie { namespace Events {

using I18n::Lang;
using I18n::Tr;

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

static const char kOpen[]  = "{{event:";
static const char kClose[] = "}}";

std::string Token(const std::string& id)
{
    return kOpen + id + kClose;
}

size_t FindToken(const std::string& s, size_t from, size_t& length, std::string& id)
{
    const size_t openLen = sizeof kOpen - 1;
    for (size_t at = s.find(kOpen, from); at != std::string::npos; at = s.find(kOpen, at + 1)) {
        size_t i = at + openLen;
        while (i < s.size() && i - (at + openLen) < 64 &&
               ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-'))
            ++i;
        if (i > at + openLen && s.compare(i, 2, kClose) == 0) {
            id = s.substr(at + openLen, i - (at + openLen));
            length = i + 2 - at;
            return at;
        }
    }
    return std::string::npos;
}

bool IsToken(const std::string& s, std::string* id)
{
    size_t len = 0;
    std::string found;
    if (FindToken(s, 0, len, found) != 0 || len != s.size()) return false;
    if (id != nullptr) *id = found;
    return true;
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

std::string FormatTime(int64_t seconds, bool active)
{
    if (seconds < 0) seconds = 0;
    char buf[64];
    const int s = (int)(seconds % 60);
    const int m = (int)((seconds / 60) % 60);
    const int h = (int)(seconds / 3600);
    if (seconds < 60) {
        snprintf(buf, sizeof buf, active ? Tr("for %d s") : Tr("in %d s"), s);
    } else if (h == 0) {
        snprintf(buf, sizeof buf, active ? Tr("for %d min") : Tr("in %d min"), m);
    } else if (m == 0) {
        snprintf(buf, sizeof buf, active ? Tr("for %d h") : Tr("in %d h"), h);
    } else {
        snprintf(buf, sizeof buf, active ? Tr("for %d h %d min") : Tr("in %d h %d min"), h, m);
    }
    return buf;
}

std::string FormatDuration(int64_t seconds)
{
    if (seconds < 0) seconds = 0;
    char buf[64];
    const int m = (int)((seconds / 60) % 60);
    const int h = (int)(seconds / 3600);
    if (h == 0)      snprintf(buf, sizeof buf, Tr("%d min"), m);
    else if (m == 0) snprintf(buf, sizeof buf, Tr("%d h"), h);
    else             snprintf(buf, sizeof buf, Tr("%d h %d min"), h, m);
    return buf;
}

}} // namespace Magpie::Events
