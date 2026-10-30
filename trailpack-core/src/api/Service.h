#pragma once
// Service.h — PURE implementation of the public ABI (public/TrailpackCoreApi.h): routes a
// request to the built-in data (Offline, Events) or the Resolver, and copies answers into the
// caller's structs honouring `structSize` (D6 / D7). dllmain only binds these methods to the
// C function table and Nexus; everything here is host-tested.

#include <atomic>
#include <cstdint>
#include <vector>

#include "TrailpackCoreApi.h"
#include "core/Lang.h"
#include "resolve/Record.h"
#include "resolve/Resolver.h"

namespace Trailpack { namespace Core {

// Copy helpers (exposed for tests). Strings are cut at a UTF-8 boundary and always
// NUL-terminated. False if `out` is null or its structSize is smaller than the head.
bool FillRecord(const Record& r, TpcStatus status, TpcRecord* out);
bool FillEvent(uint32_t index, Lang lang, TpcEvent* out);

class Service {
public:
    explicit Service(HttpFetch fetch, ResolverOptions options = {});

    void SetLang(Lang lang) { lang_ = lang; }
    Lang GetLang() const    { return lang_; }

    // Clock (Unix ms) used for cache ages and the request budget; the DLL passes the system
    // clock, tests a fake one.
    void    SetNow(int64_t nowMs) { now_ = nowMs; }
    int64_t Now() const           { return now_; }

    TpcStatus Resolve(uint8_t type, uint32_t id, const char* chatCode, TpcRecord* out);
    uint32_t  ResolveMany(uint8_t type, const uint32_t* ids, uint32_t count, TpcRecord* out);

    uint32_t EventCount() const;
    bool     EventByIndex(uint32_t index, uint8_t lang, TpcEvent* out) const;
    bool     EventById(const char* id, uint8_t lang, TpcEvent* out) const;
    bool     EventState(const char* id, int64_t now, TpcOccurrence* out) const;
    uint32_t EventNext(const char* id, int64_t now, TpcOccurrence* out, uint32_t max) const;
    uint32_t EventSearch(const char* query, TpcEventId* out, uint32_t max) const;

    // Finished background resolutions as ABI records (full size), for the RESOLVED event.
    std::vector<TpcRecord> TakeResolved();

    Resolver& GetResolver() { return resolver_; }

private:
    Resolver          resolver_;
    std::atomic<Lang> lang_{Lang::En};
    std::atomic<int64_t> now_{0};
};

}} // namespace Trailpack::Core
