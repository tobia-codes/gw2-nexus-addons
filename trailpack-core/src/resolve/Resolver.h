#pragma once
// Resolver.h — PURE resolution service for API-backed links (item, skin, skill, trait,
// recipe): warm cache, request queue with batching, token bucket, retries, skill wiki
// fallback, recipe second step, disk-cache (de)serialisation. No thread and no HTTP of its
// own: the DLL drives Step() from one worker thread with an injected fetch function, and
// tests drive it synchronously with canned responses.
//
// Contract (PLAN D8, test checklist "terminal outcome"): Resolve() answers Warm or Failed
// right away, or Pending — and every key answered Pending gets exactly one completion later
// (resolved or failed), unless the resolver is shut down first.
//
// Thread safety: Resolve / TakeCompletions / SetOffline / stats / cache calls may run on any
// thread concurrently with Step(); one internal mutex guards the state and is never held
// while a request is in flight.

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/TokenBucket.h"
#include "resolve/Parse.h"
#include "resolve/Record.h"

namespace Trailpack { namespace Core {

// HTTP GET: returns the status code (0 = no response) and fills `body`.
using HttpFetch = std::function<int(const std::string& url, std::string& body)>;

enum class Answer { Warm, Pending, Failed };

struct ResolverOptions {
    double  perSecond      = 2.0;                      // D10
    double  burst          = 20.0;
    int64_t failCooldownMs = 5 * 60 * 1000;            // a failed id is not asked again before
    int64_t maxAgeMs       = 30LL * 24 * 3600 * 1000;  // D11: entries expire after 30 days
    size_t  maxBatch       = 200;                      // ids per request (API limit)
    int64_t batchWindowMs  = 50;                       // wait this long to collect more ids
    int     maxAttempts    = 3;                        // transient failures before "failed"
};

struct Completion {
    Record record;   // type, id and lang always set
    bool   ok = false;
};

struct ResolverStats {
    size_t      queued = 0;              // ids waiting or in flight
    size_t      cached[2] = {0, 0};      // warm entries per language (En, De)
    int         requestsLastMinute = 0;
    std::string lastError;               // "" when the last request went fine
    uint32_t    gameBuild = 0;           // 0 = not known yet
    bool        offline = false;
};

class Resolver {
public:
    explicit Resolver(HttpFetch fetch, ResolverOptions options = {});

    // True for the link types this resolver serves.
    static bool Serves(uint8_t type);

    // ── Any thread ─────────────────────────────────────────────────────────
    // Warm: `out` filled (a stale entry is served and refreshed in the background).
    // Failed: invalid id, unsupported type, offline and not cached, or a failure in cooldown.
    // Pending: queued; a completion follows.
    Answer Resolve(uint8_t type, uint32_t id, Lang lang, int64_t nowMs, Record* out);

    // Completions since the last call, in order.
    std::vector<Completion> TakeCompletions();

    // Offline: no requests at all; pending keys fail now, cold keys fail right away.
    void SetOffline(bool offline);
    bool Offline() const { return offline_; }

    ResolverStats Stats(int64_t nowMs) const;

    // Stop for good: waits for a request in flight, drops queued work and completions; every
    // call afterwards is a safe no-op (Resolve answers Failed).
    void Shutdown();

    // ── Worker thread ──────────────────────────────────────────────────────
    // Does at most one request if one is due and the budget allows. Returns the time (ms) at
    // which it wants to be called again (nowMs if more work is ready).
    int64_t Step(int64_t nowMs);

    // ── Disk cache ─────────────────────────────────────────────────────────
    // One document per language and type (D9). Load keeps entries of an unknown version out
    // (they are refetched), and a missing field reads back as its default ("unknown").
    std::string SaveCache(Lang lang, uint8_t type) const;
    bool        LoadCache(Lang lang, uint8_t type, const std::string& json);
    // (lang, type) pairs changed since the last call.
    std::vector<std::pair<Lang, uint8_t>> TakeDirty();
    void ClearCache();

    static constexpr int kCacheVersion = 1;

private:
    struct Entry {
        Record  record;
        bool    warm      = false;
        bool    pending   = false;
        int64_t fetchedAt = 0;      // ms
        uint32_t build    = 0;      // game build when fetched (0 = unknown)
        int64_t failedAt  = -1;     // ms of the last terminal failure, -1 = none
        int     attempts  = 0;      // transient failures of the current round
    };
    struct Queued { uint64_t key; int64_t since; };
    struct WikiJob { uint64_t key; const char* context; };
    struct RecipeJob {
        uint64_t key;
        Parse::Recipe recipe;
        bool itemsDone = false;
        bool guildDone = false;
        std::map<uint32_t, std::string> guildNames;
    };

    static uint64_t Key(uint8_t type, Lang lang, uint32_t id);
    static uint8_t  KeyType(uint64_t key);
    static Lang     KeyLang(uint64_t key);
    static uint32_t KeyId(uint64_t key);

    // All with mutex_ held.
    void Enqueue(uint64_t key, int64_t nowMs);
    void Finish(uint64_t key, bool ok, int64_t nowMs);
    void Store(Record&& rec, int64_t nowMs);
    void Retry(const std::vector<uint64_t>& keys, int64_t nowMs, const std::string& error);
    void NoteRequest(int64_t nowMs);

    // One step of each kind; return true if they did (or tried) a request.
    bool StepBuild(int64_t nowMs);
    bool StepRecipe(int64_t nowMs);
    bool StepWiki(int64_t nowMs);
    bool StepBatch(int64_t nowMs);

    int Fetch(const std::string& url, std::string& body);   // no lock held

    HttpFetch       fetch_;
    ResolverOptions opt_;

    mutable std::mutex mutex_;
    std::mutex         stepMutex_;     // held for a whole Step (Shutdown waits on it)
    std::atomic<bool>  shutdown_{false};
    std::atomic<bool>  offline_{false};

    TokenBucket bucket_;
    std::unordered_map<uint64_t, Entry> entries_;
    std::deque<Queued>    queue_;
    std::deque<WikiJob>   wiki_;
    std::deque<RecipeJob> recipes_;
    std::vector<Completion> completions_;
    std::set<std::pair<int, int>> dirty_;
    std::deque<int64_t>   requestTimes_;
    std::string           lastError_;
    uint32_t              build_ = 0;
    bool                  buildChecked_ = false;
    int64_t               buildRetryAt_ = 0;
};

}} // namespace Trailpack::Core
