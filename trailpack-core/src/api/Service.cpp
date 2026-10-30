// Service.cpp — see Service.h.
#include "api/Service.h"

#include <cstddef>
#include <cstring>
#include <string>

#include "chatlinks/ChatLinks.h"
#include "events/EventSchedule.h"
#include "resolve/Offline.h"
#include "resolve/Text.h"

namespace Trailpack { namespace Core {

namespace {

template <size_t N>
void Copy(char (&dst)[N], const std::string& src)
{
    const std::string s = Text::TruncateUtf8(src, N - 1);
    std::memcpy(dst, s.data(), s.size());
    dst[s.size()] = '\0';
}

template <size_t N>
void Copy(char (&dst)[N], const char* src)
{
    Copy(dst, std::string(src != nullptr ? src : ""));
}

// Copy a fully built struct into the caller's one: at most its structSize bytes.
template <typename T>
bool Deliver(const T& full, T* out)
{
    if (out == nullptr) return false;
    const uint32_t want = out->structSize;
    if (want < 8) return false;                    // not even the head
    const uint32_t n = want < sizeof(T) ? want : static_cast<uint32_t>(sizeof(T));
    std::memcpy(out, &full, n);
    out->structSize = n;
    return true;
}

TpcRecord Empty(uint8_t type, uint32_t id, Lang lang, TpcStatus status)
{
    TpcRecord r;
    std::memset(&r, 0, sizeof r);
    r.structSize = sizeof r;
    r.linkType   = type;
    r.id         = id;
    r.lang       = static_cast<uint8_t>(lang);
    r.status     = status;
    return r;
}

bool Deliver(uint8_t type, uint32_t id, Lang lang, TpcStatus status, TpcRecord* out)
{
    return Deliver(Empty(type, id, lang, status), out);
}

const Events::EventDef* ById(const char* id)
{
    return id != nullptr ? Events::Find(id) : nullptr;
}

} // namespace

bool FillRecord(const Record& r, TpcStatus status, TpcRecord* out)
{
    TpcRecord full = Empty(r.type, r.id, r.lang, status);
    Copy(full.name, r.name);
    Copy(full.iconUrl, r.icon);
    Copy(full.description, r.description);
    for (const auto& f : r.facts) {
        if (full.factCount >= TPC_MAX_FACTS) break;
        TpcFact& dst = full.facts[full.factCount++];
        Copy(dst.icon, f.icon);
        Copy(dst.text, f.text);
    }
    full.rarity             = static_cast<uint8_t>(r.rarity);
    full.bound              = static_cast<uint8_t>(r.bound);
    full.noSell             = r.noSell ? 1 : 0;
    full.tradeable          = r.tradeable ? 1 : 0;
    full.vendorValue        = r.vendorValue;
    full.recipeOutputItemId = r.outputItemId;
    full.recipeOutputCount  = r.outputCount;
    full.recipeMinRating    = r.minRating;
    Copy(full.poiMapName, r.mapName);
    full.poiKind            = r.poiKind;
    return Deliver(full, out);
}

bool FillEvent(uint32_t index, Lang lang, TpcEvent* out)
{
    if (index >= Events::kEventCount) return false;
    const Events::EventDef& e = Events::kEvents[index];
    TpcEvent full;
    std::memset(&full, 0, sizeof full);
    full.structSize = sizeof full;
    Copy(full.id, e.id);
    Copy(full.name, Events::Name(e, lang));
    Copy(full.category, Events::CategoryName(e.category, lang));
    Copy(full.categoryShort, Events::CategoryShort(e.category, lang));
    Copy(full.wikiPage, e.wikiPage);
    Copy(full.maps, Events::Maps(e, lang));
    full.occurrencesPerDay = e.count;
    for (int i = 0; i < e.placeCount && full.placeCount < TPC_MAX_EVENT_PLACES; ++i) {
        TpcEventPlace& p = full.places[full.placeCount++];
        Copy(p.map, Events::MapName(e.places[i], lang));
        Copy(p.waypoint, e.places[i].waypoint);
        Copy(p.waypointName, Events::WaypointName(e.places[i], lang));
    }
    return Deliver(full, out);
}

Service::Service(HttpFetch fetch, ResolverOptions options)
    : resolver_(std::move(fetch), options) {}

TpcStatus Service::Resolve(uint8_t type, uint32_t id, const char* chatCode, TpcRecord* out)
{
    const Lang lang = lang_;
    if (Offline::Serves(type)) {
        Record r;
        const bool ok = type == ChatLinks::LINK_BUILD ? Offline::Build(chatCode != nullptr ? chatCode : "", lang, r)
                                                      : Offline::Poi(id, lang, r);
        if (!ok) { Deliver(type, id, lang, TPC_FAILED, out); return TPC_FAILED; }
        FillRecord(r, TPC_RESOLVED, out);
        return TPC_RESOLVED;
    }
    Record r;
    switch (resolver_.Resolve(type, id, lang, now_, &r)) {
        case Answer::Warm:
            FillRecord(r, TPC_RESOLVED, out);
            return TPC_RESOLVED;
        case Answer::Pending:
            Deliver(type, id, lang, TPC_NOT_READY, out);
            return TPC_NOT_READY;
        default:
            Deliver(type, id, lang, TPC_FAILED, out);
            return TPC_FAILED;
    }
}

uint32_t Service::ResolveMany(uint8_t type, const uint32_t* ids, uint32_t count, TpcRecord* out)
{
    if (ids == nullptr || out == nullptr || count == 0) return 0;
    const uint32_t stride = out->structSize;
    if (stride < 8) return 0;
    uint32_t warm = 0;
    auto* base = reinterpret_cast<unsigned char*>(out);
    for (uint32_t i = 0; i < count; ++i) {
        auto* rec = reinterpret_cast<TpcRecord*>(base + size_t(i) * stride);
        rec->structSize = stride;
        if (Resolve(type, ids[i], nullptr, rec) == TPC_RESOLVED) ++warm;
    }
    return warm;
}

uint32_t Service::EventCount() const
{
    return static_cast<uint32_t>(Events::kEventCount);
}

bool Service::EventByIndex(uint32_t index, uint8_t lang, TpcEvent* out) const
{
    return FillEvent(index, lang == TPC_LANG_DE ? Lang::De : Lang::En, out);
}

bool Service::EventById(const char* id, uint8_t lang, TpcEvent* out) const
{
    const Events::EventDef* e = ById(id);
    return e != nullptr && EventByIndex(static_cast<uint32_t>(e - Events::kEvents), lang, out);
}

bool Service::EventState(const char* id, int64_t now, TpcOccurrence* out) const
{
    const Events::EventDef* e = ById(id);
    if (e == nullptr || out == nullptr) return false;
    const Events::Status st = Events::State(*e, now);
    *out = TpcOccurrence{};
    out->start  = st.start;
    out->end    = st.end;
    out->place  = st.place;
    out->active = st.active ? 1 : 0;
    return true;
}

uint32_t Service::EventNext(const char* id, int64_t now, TpcOccurrence* out, uint32_t max) const
{
    const Events::EventDef* e = ById(id);
    if (e == nullptr || out == nullptr || max == 0) return 0;
    const auto next = Events::NextOccurrences(*e, now, static_cast<int>(max > 1000 ? 1000 : max));
    for (size_t i = 0; i < next.size(); ++i) {
        out[i] = TpcOccurrence{};
        out[i].start = next[i].start;
        out[i].end   = next[i].end;
        out[i].place = next[i].place;
    }
    return static_cast<uint32_t>(next.size());
}

uint32_t Service::EventSearch(const char* query, TpcEventId* out, uint32_t max) const
{
    const auto hits = Events::Search(query != nullptr ? query : "");
    for (uint32_t i = 0; out != nullptr && i < max && i < hits.size(); ++i) Copy(out[i].id, hits[i]->id);
    return static_cast<uint32_t>(hits.size());
}

std::vector<TpcRecord> Service::TakeResolved()
{
    std::vector<TpcRecord> out;
    for (const auto& c : resolver_.TakeCompletions()) {
        TpcRecord rec;
        rec.structSize = sizeof rec;
        if (c.ok) FillRecord(c.record, TPC_RESOLVED, &rec);
        else      Deliver(c.record.type, c.record.id, c.record.lang, TPC_FAILED, &rec);
        out.push_back(rec);
    }
    return out;
}

}} // namespace Trailpack::Core
