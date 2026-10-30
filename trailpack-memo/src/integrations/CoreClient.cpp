// CoreClient.cpp — see CoreClient.h.
//
// The RESOLVED handler runs on the render thread (Core raises it from its pre-render pump),
// but the cache is still guarded by a mutex so nothing depends on that.

#include "integrations/CoreClient.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "chatlinks/ChatLinks.h"
#include "core/I18n.h"

namespace Trailpack { namespace CoreClient {

namespace {

AddonAPI_t* APIDefs = nullptr;

std::mutex                              g_mutex;
std::unordered_map<uint64_t, TpcRecord> g_records;    // warm API answers, key (type, id)
std::vector<Event>                      g_events;     // in data order
I18n::Lang                              g_eventsLang = I18n::Lang::En;
bool                                    g_eventsLoaded = false;   // render thread only
std::atomic<bool>                       g_eventsStale{true};      // set by the event handlers

uint64_t Key(uint8_t type, uint32_t id) { return (uint64_t(type) << 32) | id; }

// Re-resolve per call; NEVER keep the result (its pointers are zeroed when Core unloads).
const TpcApi* Raw() {
    if (!APIDefs || !APIDefs->DataLink_Get) return nullptr;
    return static_cast<const TpcApi*>(APIDefs->DataLink_Get(TRAILPACK_CORE_DATALINK));
}

const TpcApi* Api() {
    const TpcApi* api = Raw();
    return (api && api->apiVersion >= kMinApiVersion && api->Resolve) ? api : nullptr;
}

// May run on any thread: records are guarded; the event list is only marked stale and
// rebuilt by Frame() on the render thread, so no pointer into it dies mid-frame.
void DropAll() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_records.clear();
    g_eventsStale = true;
}

// A background answer landed: payload valid only now -> copy.
void OnResolved(void* payload) {
    if (!payload) return;
    const auto* rec = static_cast<const TpcRecord*>(payload);
    if (rec->structSize < sizeof(uint32_t) * 3 || rec->status != TPC_RESOLVED) return;
    TpcRecord copy;
    std::memset(&copy, 0, sizeof copy);
    std::memcpy(&copy, rec, rec->structSize < sizeof copy ? rec->structSize : sizeof copy);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_records[Key(copy.linkType, copy.id)] = copy;
}

// (Re)appearance: drop what an older Core answered, it may have changed.
void OnReady(void*)     { DropAll(); }
void OnUnloading(void*) { DropAll(); }

TpcRecord Blank() {
    TpcRecord r;
    std::memset(&r, 0, sizeof r);
    r.structSize = sizeof r;
    return r;
}

} // namespace

// Load all events in Memo's language (once per Core appearance / language switch).
void Frame() {
    const I18n::Lang lang = I18n::Current();
    if (!g_eventsStale && g_eventsLoaded && g_eventsLang == lang) return;
    g_eventsStale = false;
    std::vector<Event> list;
    const TpcApi* api = Api();
    if (api && api->EventCount && api->EventByIndex) {
        const uint8_t tlang = lang == I18n::Lang::De ? TPC_LANG_DE : TPC_LANG_EN;
        const uint32_t n = api->EventCount();
        for (uint32_t i = 0; i < n; ++i) {
            TpcEvent ev;
            std::memset(&ev, 0, sizeof ev);
            ev.structSize = sizeof ev;
            if (!api->EventByIndex(i, tlang, &ev)) continue;
            Event e;
            e.id = ev.id; e.name = ev.name; e.category = ev.category; e.categoryShort = ev.categoryShort;
            e.wikiPage = ev.wikiPage; e.maps = ev.maps;
            for (int p = 0; p < ev.placeCount && p < TPC_MAX_EVENT_PLACES; ++p)
                e.places.push_back({ ev.places[p].map, ev.places[p].waypoint, ev.places[p].waypointName });
            if (e.places.empty()) e.places.push_back({});
            list.push_back(std::move(e));
        }
    }
    g_events       = std::move(list);
    g_eventsLang   = lang;
    g_eventsLoaded = api != nullptr;   // without Core, try again next frame
}

void Init(AddonAPI_t* api) {
    APIDefs = api;
    if (!APIDefs) return;
    APIDefs->Events_Subscribe(EV_TRAILPACK_CORE_READY,     OnReady);
    APIDefs->Events_Subscribe(EV_TRAILPACK_CORE_UNLOADING, OnUnloading);
    APIDefs->Events_Subscribe(EV_TRAILPACK_CORE_RESOLVED,  OnResolved);
    APIDefs->Events_Raise(EV_TRAILPACK_CORE_PING, nullptr);   // Core loaded first?
}

void Shutdown() {
    g_events.clear();
    g_eventsLoaded = false;
    if (APIDefs) {
        APIDefs->Events_Unsubscribe(EV_TRAILPACK_CORE_READY,     OnReady);
        APIDefs->Events_Unsubscribe(EV_TRAILPACK_CORE_UNLOADING, OnUnloading);
        APIDefs->Events_Unsubscribe(EV_TRAILPACK_CORE_RESOLVED,  OnResolved);
    }
    DropAll();
    APIDefs = nullptr;
}

State Status() {
    const TpcApi* api = Raw();
    if (!api || api->apiVersion == 0 || !api->Resolve) return State::Absent;   // 0: unloaded
    return api->apiVersion >= kMinApiVersion ? State::Ready : State::TooOld;
}

bool Resolve(uint8_t linkType, uint32_t id, const char* chatCode, TpcRecord& out) {
    const TpcApi* api = Api();
    if (!api) return false;

    // Built-in answers (PoIs, builds) are synchronous and cheap: no cache needed.
    if (linkType == ChatLinks::LINK_MAP || linkType == ChatLinks::LINK_BUILD) {
        out = Blank();
        return api->Resolve(linkType, id, chatCode, &out) == TPC_RESOLVED;
    }

    const uint8_t lang = api->ActiveLanguage ? api->ActiveLanguage() : TPC_LANG_EN;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_records.find(Key(linkType, id));
        if (it != g_records.end() && it->second.lang == lang) {
            out = it->second;
            return true;
        }
    }
    TpcRecord rec = Blank();
    if (api->Resolve(linkType, id, chatCode, &rec) != TPC_RESOLVED) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_records[Key(linkType, id)] = rec;
    }
    out = rec;
    return true;
}

void Prefetch(uint32_t itemId) {
    if (itemId == 0) return;
    TpcRecord unused;
    Resolve(ChatLinks::LINK_ITEM, itemId, nullptr, unused);
}

const std::vector<Event>& Events() {
    return g_events;
}

const Event* FindEvent(const std::string& id) {
    for (const Event& e : Events())
        if (e.id == id) return &e;
    return nullptr;
}

bool EventState(const std::string& id, int64_t now, Occurrence& out) {
    const TpcApi* api = Api();
    if (!api || !api->EventState) return false;
    TpcOccurrence o{};
    if (!api->EventState(id.c_str(), now, &o)) return false;
    out = { o.start, o.end, o.place, o.active != 0 };
    return true;
}

std::vector<Occurrence> EventNext(const std::string& id, int64_t now, int n) {
    std::vector<Occurrence> out;
    const TpcApi* api = Api();
    if (!api || !api->EventNext || n <= 0) return out;
    std::vector<TpcOccurrence> buf(n);
    const uint32_t got = api->EventNext(id.c_str(), now, buf.data(), (uint32_t)n);
    for (uint32_t i = 0; i < got && i < (uint32_t)n; ++i)
        out.push_back({ buf[i].start, buf[i].end, buf[i].place, false });
    return out;
}

std::vector<const Event*> SearchEvents(const std::string& query) {
    std::vector<const Event*> out;
    const TpcApi* api = Api();
    if (!api || !api->EventSearch) return out;
    std::vector<TpcEventId> ids(Events().size() + 1);
    const uint32_t n = api->EventSearch(query.c_str(), ids.data(), (uint32_t)ids.size());
    for (uint32_t i = 0; i < n && i < ids.size(); ++i)
        if (const Event* e = FindEvent(ids[i].id)) out.push_back(e);
    return out;
}

}} // namespace Trailpack::CoreClient
