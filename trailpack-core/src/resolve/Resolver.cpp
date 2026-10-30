// Resolver.cpp — see Resolver.h.
#include "resolve/Resolver.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "chatlinks/ChatLinks.h"

namespace Trailpack { namespace Core {

using nlohmann::json;
namespace CL = ChatLinks;

namespace {

const char kApi[] = "https://api.guildwars2.com/v2/";

const char* Endpoint(uint8_t type)
{
    switch (type) {
        case CL::LINK_ITEM:   return "items";
        case CL::LINK_SKIN:   return "skins";
        case CL::LINK_SKILL:  return "skills";
        case CL::LINK_TRAIT:  return "traits";
        case CL::LINK_RECIPE: return "recipes";
        default:              return nullptr;
    }
}

std::string IdList(const std::vector<uint32_t>& ids)
{
    std::string s;
    for (uint32_t id : ids) {
        if (!s.empty()) s += ',';
        s += std::to_string(id);
    }
    return s;
}

bool Transient(int status)
{
    return status == 0 || status == 429 || status >= 500;
}

} // namespace

// ── Keys ────────────────────────────────────────────────────────────────────

uint64_t Resolver::Key(uint8_t type, Lang lang, uint32_t id)
{
    return (uint64_t(type) << 40) | (uint64_t(lang) << 32) | id;
}
uint8_t  Resolver::KeyType(uint64_t key) { return uint8_t(key >> 40); }
Lang     Resolver::KeyLang(uint64_t key) { return Lang(uint8_t(key >> 32)); }
uint32_t Resolver::KeyId(uint64_t key)   { return uint32_t(key); }

Resolver::Resolver(HttpFetch fetch, ResolverOptions options)
    : fetch_(std::move(fetch)), opt_(options), bucket_(options.perSecond, options.burst) {}

bool Resolver::Serves(uint8_t type)
{
    return Endpoint(type) != nullptr;
}

// ── Any thread ──────────────────────────────────────────────────────────────

Answer Resolver::Resolve(uint8_t type, uint32_t id, Lang lang, int64_t nowMs, Record* out)
{
    if (shutdown_ || !Serves(type) || id == 0) return Answer::Failed;
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t key = Key(type, lang, id);
    Entry& e = entries_[key];

    if (e.warm) {
        if (out) *out = e.record;
        const bool old   = nowMs - e.fetchedAt > opt_.maxAgeMs;
        const bool patch = build_ != 0 && e.build != 0 && e.build != build_;
        const bool resting = e.failedAt >= 0 && nowMs - e.failedAt < opt_.failCooldownMs;
        if ((old || patch) && !e.pending && !resting && !offline_) Enqueue(key, nowMs);
        return Answer::Warm;
    }
    if (e.pending) return Answer::Pending;
    if (e.failedAt >= 0 && nowMs - e.failedAt < opt_.failCooldownMs) return Answer::Failed;
    if (offline_) return Answer::Failed;
    Enqueue(key, nowMs);
    return Answer::Pending;
}

std::vector<Completion> Resolver::TakeCompletions()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Completion> out;
    out.swap(completions_);
    return out;
}

void Resolver::SetOffline(bool offline)
{
    std::lock_guard<std::mutex> lock(mutex_);
    offline_ = offline;
    if (!offline) return;
    // Nothing queued may stay pending forever: fail it now (warm entries keep their data).
    std::vector<uint64_t> keys;
    for (const auto& q : queue_)   keys.push_back(q.key);
    for (const auto& w : wiki_)    keys.push_back(w.key);
    for (const auto& r : recipes_) keys.push_back(r.key);
    queue_.clear();
    wiki_.clear();
    recipes_.clear();
    for (uint64_t k : keys) {
        Entry& e = entries_[k];
        if (!e.pending) continue;
        e.pending = false;
        if (!e.warm) completions_.push_back({e.record.id ? e.record : Record{KeyType(k), KeyId(k), KeyLang(k)}, false});
    }
}

ResolverStats Resolver::Stats(int64_t nowMs) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    ResolverStats s;
    for (const auto& kv : entries_) {
        if (kv.second.pending) ++s.queued;
        if (kv.second.warm) ++s.cached[KeyLang(kv.first) == Lang::De ? 1 : 0];
    }
    for (int64_t t : requestTimes_)
        if (nowMs - t < 60 * 1000) ++s.requestsLastMinute;
    s.lastError = lastError_;
    s.gameBuild = build_;
    s.offline   = offline_;
    return s;
}

void Resolver::Shutdown()
{
    shutdown_ = true;
    std::lock_guard<std::mutex> step(stepMutex_);   // waits for a request in flight
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    wiki_.clear();
    recipes_.clear();
    completions_.clear();
}

// ── Internals (mutex_ held) ─────────────────────────────────────────────────

void Resolver::Enqueue(uint64_t key, int64_t nowMs)
{
    Entry& e = entries_[key];
    e.pending  = true;
    e.attempts = 0;
    queue_.push_back({key, nowMs});
}

void Resolver::Finish(uint64_t key, bool ok, int64_t nowMs)
{
    Entry& e = entries_[key];
    const bool wasPending = e.pending;
    e.pending  = false;
    e.attempts = 0;
    if (ok) {
        // Only keys someone waits for get an event; extra records in a response (or the
        // items a recipe needed) are stored silently.
        if (wasPending) completions_.push_back({e.record, true});
        return;
    }
    e.failedAt = nowMs;
    // A failed background refresh keeps the warm data and says nothing.
    if (!e.warm && wasPending) {
        Record r;
        r.type = KeyType(key);
        r.id   = KeyId(key);
        r.lang = KeyLang(key);
        completions_.push_back({r, false});
    }
}

void Resolver::Store(Record&& rec, int64_t nowMs)
{
    const uint64_t key = Key(rec.type, rec.lang, rec.id);
    Entry& e = entries_[key];
    e.record    = std::move(rec);
    e.warm      = true;
    e.fetchedAt = nowMs;
    e.build     = build_;
    e.failedAt  = -1;
    dirty_.insert({int(KeyLang(key)), int(KeyType(key))});
    Finish(key, true, nowMs);
}

void Resolver::Retry(const std::vector<uint64_t>& keys, int64_t nowMs, const std::string& error)
{
    lastError_ = error;
    for (uint64_t k : keys) {
        Entry& e = entries_[k];
        if (++e.attempts >= opt_.maxAttempts) Finish(k, false, nowMs);
        else queue_.push_back({k, nowMs});
    }
}

void Resolver::NoteRequest(int64_t nowMs)
{
    requestTimes_.push_back(nowMs);
    while (!requestTimes_.empty() && nowMs - requestTimes_.front() >= 60 * 1000) requestTimes_.pop_front();
}

int Resolver::Fetch(const std::string& url, std::string& body)
{
    body.clear();
    return fetch_ ? fetch_(url, body) : 0;
}

// ── Worker ──────────────────────────────────────────────────────────────────

int64_t Resolver::Step(int64_t nowMs)
{
    std::lock_guard<std::mutex> step(stepMutex_);
    if (shutdown_) return nowMs + 1000;
    if (!offline_ && (StepBuild(nowMs) || StepRecipe(nowMs) || StepWiki(nowMs) || StepBatch(nowMs)))
        return nowMs;

    std::lock_guard<std::mutex> lock(mutex_);
    if (offline_) return nowMs + 1000;
    const bool jobs = !wiki_.empty() || !recipes_.empty() || !buildChecked_;
    if (!jobs && queue_.empty()) return nowMs + 1000;
    int64_t at = bucket_.NextTokenAt(nowMs);
    if (!jobs) at = std::max(at, queue_.front().since + opt_.batchWindowMs);
    if (!buildChecked_ && wiki_.empty() && recipes_.empty() && queue_.empty()) at = std::max(at, buildRetryAt_);
    return std::max(at, nowMs + 1);
}

bool Resolver::StepBuild(int64_t nowMs)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buildChecked_ || nowMs < buildRetryAt_ || !bucket_.TryTake(nowMs)) return false;
        NoteRequest(nowMs);
    }
    std::string body;
    const int status = Fetch(std::string(kApi) + "build", body);
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return true;
    const json j = json::parse(body, nullptr, false);
    if (status == 200 && j.is_object() && j.contains("id") && j["id"].is_number_unsigned()) {
        build_ = j["id"].get<uint32_t>();
        buildChecked_ = true;
        bucket_.Success();
    } else {
        // Not knowing the build only delays the patch check; try again in a minute.
        buildRetryAt_ = nowMs + 60 * 1000;
        if (Transient(status)) bucket_.Backoff(nowMs);
    }
    return true;
}

bool Resolver::StepRecipe(int64_t nowMs)
{
    std::string url;
    bool guild = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recipes_.empty()) return false;
        RecipeJob& job = recipes_.front();
        const Lang lang = KeyLang(job.key);
        if (!job.itemsDone) {
            std::vector<uint32_t> missing;
            for (uint32_t id : Parse::RecipeItemIds(job.recipe)) {
                auto it = entries_.find(Key(CL::LINK_ITEM, lang, id));
                if ((it == entries_.end() || !it->second.warm) &&
                    std::find(missing.begin(), missing.end(), id) == missing.end())
                    missing.push_back(id);
            }
            if (missing.empty()) job.itemsDone = true;
            else url = std::string(kApi) + "items?ids=" + IdList(missing) + "&lang=" + ApiCode(lang);
        }
        if (url.empty() && !job.guildDone) {
            const auto ids = Parse::RecipeGuildIds(job.recipe);
            if (ids.empty()) job.guildDone = true;
            else { url = std::string(kApi) + "guild/upgrades?ids=" + IdList(ids) + "&lang=" + ApiCode(lang); guild = true; }
        }
        if (url.empty()) {
            // Everything known: build the record from the warm item entries.
            std::map<uint32_t, std::string> names;
            const Record* output = nullptr;
            for (uint32_t id : Parse::RecipeItemIds(job.recipe)) {
                auto it = entries_.find(Key(CL::LINK_ITEM, lang, id));
                if (it == entries_.end() || !it->second.warm) continue;
                names[id] = it->second.record.name;
                if (id == job.recipe.outputItemId) output = &it->second.record;
            }
            Record rec = Parse::RecipeRecord(job.recipe, output, names, job.guildNames, lang);
            rec.lang = lang;
            recipes_.pop_front();
            Store(std::move(rec), nowMs);
            return true;
        }
        if (!bucket_.TryTake(nowMs)) return false;
        NoteRequest(nowMs);
    }

    std::string body;
    const int status = Fetch(url, body);
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_ || recipes_.empty()) return true;
    RecipeJob& job = recipes_.front();
    const Lang lang = KeyLang(job.key);
    bool ok = status == 200 || status == 206;
    if (ok && guild) {
        ok = Parse::Names(body, job.guildNames);
        job.guildDone = true;                              // best effort either way
    } else if (ok) {
        std::vector<Record> items;
        try { ok = Parse::Items(body, lang, items); } catch (...) { ok = false; }
        for (auto& r : items) Store(std::move(r), nowMs);
        if (ok) job.itemsDone = true;
    }
    if (ok) {
        bucket_.Success();
        lastError_.clear();
    } else if (Transient(status) || status == 200) {
        bucket_.Backoff(nowMs);
        lastError_ = "recipe details: HTTP " + std::to_string(status);
        Entry& e = entries_[job.key];
        if (++e.attempts >= opt_.maxAttempts) {            // give up on names: show ids
            job.itemsDone = job.guildDone = true;
            e.attempts = 0;
        }
    } else {
        job.itemsDone = job.guildDone = true;              // 404 etc.: names stay "#id"
    }
    return true;
}

bool Resolver::StepWiki(int64_t nowMs)
{
    WikiJob job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (wiki_.empty() || !bucket_.TryTake(nowMs)) return false;
        job = wiki_.front();
        wiki_.pop_front();
        NoteRequest(nowMs);
    }
    std::string body;
    const int status = Fetch(Parse::WikiAskUrl(KeyId(job.key), job.context), body);
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return true;

    Record rec;
    rec.type = KeyType(job.key);
    rec.id   = KeyId(job.key);
    rec.lang = KeyLang(job.key);
    Parse::WikiResult result = Parse::WikiResult::Error;
    if (status == 200) {
        try { result = Parse::WikiSkill(body, rec); } catch (...) { result = Parse::WikiResult::Error; }
    }
    if (result == Parse::WikiResult::Found) {
        bucket_.Success();
        lastError_.clear();
        Store(std::move(rec), nowMs);
    } else if (result == Parse::WikiResult::Empty) {
        bucket_.Success();
        // Not a skill page: effect / buff links are 0x06 links catalogued as effects.
        if (std::string(job.context) == "Skill") wiki_.push_back({job.key, "Effect"});
        else Finish(job.key, false, nowMs);
    } else {
        bucket_.Backoff(nowMs);
        lastError_ = "wiki: " + (status == 200 ? std::string("unexpected answer (maintenance?)")
                                               : "HTTP " + std::to_string(status));
        Entry& e = entries_[job.key];
        if (++e.attempts >= opt_.maxAttempts) Finish(job.key, false, nowMs);
        else wiki_.push_back(job);
    }
    return true;
}

bool Resolver::StepBatch(int64_t nowMs)
{
    std::vector<uint64_t> keys;
    std::string url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Drop keys that are no longer waiting (finished elsewhere).
        while (!queue_.empty() && !entries_[queue_.front().key].pending) queue_.pop_front();
        if (queue_.empty() || nowMs - queue_.front().since < opt_.batchWindowMs) return false;
        if (bucket_.NextTokenAt(nowMs) > nowMs) return false;

        const uint8_t type = KeyType(queue_.front().key);
        const Lang    lang = KeyLang(queue_.front().key);
        std::vector<uint32_t> ids;
        for (auto it = queue_.begin(); it != queue_.end() && keys.size() < opt_.maxBatch;) {
            if (KeyType(it->key) == type && KeyLang(it->key) == lang && entries_[it->key].pending) {
                if (std::find(keys.begin(), keys.end(), it->key) == keys.end()) {
                    keys.push_back(it->key);
                    ids.push_back(KeyId(it->key));
                }
                it = queue_.erase(it);
            } else {
                ++it;
            }
        }
        if (keys.empty() || !bucket_.TryTake(nowMs)) {
            for (uint64_t k : keys) queue_.push_front({k, nowMs - opt_.batchWindowMs});
            return false;
        }
        url = std::string(kApi) + Endpoint(type) + "?ids=" + IdList(ids) + "&lang=" + ApiCode(lang);
        NoteRequest(nowMs);
    }

    std::string body;
    const int status = Fetch(url, body);
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return true;

    const uint8_t type = KeyType(keys.front());
    const Lang    lang = KeyLang(keys.front());
    std::vector<Record> records;
    std::vector<Parse::Recipe> recipes;
    bool parsed = false;
    if (status == 200 || status == 206) {
        try {
            switch (type) {
                case CL::LINK_ITEM:   parsed = Parse::Items(body, lang, records); break;
                case CL::LINK_SKIN:   parsed = Parse::Skins(body, lang, records); break;
                case CL::LINK_SKILL:  parsed = Parse::Skills(body, lang, records); break;
                case CL::LINK_TRAIT:  parsed = Parse::Traits(body, lang, records); break;
                case CL::LINK_RECIPE: parsed = Parse::Recipes(body, recipes); break;
            }
        } catch (...) {
            parsed = false;
        }
    }
    // 404: "all ids provided are invalid" — every id is simply unknown.
    if (!parsed && status != 404) {
        bucket_.Backoff(nowMs);
        Retry(keys, nowMs, std::string(Endpoint(type)) + ": " +
                           (status == 0 ? std::string("no connection") : "HTTP " + std::to_string(status)));
        return true;
    }
    bucket_.Success();
    lastError_.clear();

    std::set<uint32_t> found;
    for (auto& r : records) {
        r.lang = lang;
        found.insert(r.id);
        Store(std::move(r), nowMs);
    }
    for (auto& rc : recipes) {
        const uint64_t key = Key(CL::LINK_RECIPE, lang, rc.id);
        if (!entries_[key].pending) continue;
        found.insert(rc.id);
        recipes_.push_back({key, std::move(rc)});
    }
    for (uint64_t k : keys) {
        if (found.count(KeyId(k))) continue;
        if (type == CL::LINK_SKILL) wiki_.push_back({k, "Skill"});   // .dat-only skills, effects
        else Finish(k, false, nowMs);
    }
    return true;
}

// ── Disk cache ──────────────────────────────────────────────────────────────

std::string Resolver::SaveCache(Lang lang, uint8_t type) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    json list = json::array();
    for (const auto& kv : entries_) {
        const Entry& e = kv.second;
        if (!e.warm || KeyType(kv.first) != type || KeyLang(kv.first) != lang) continue;
        const Record& r = e.record;
        json facts = json::array();
        for (const auto& f : r.facts) facts.push_back(json::array({f.icon, f.text}));
        json o = {{"id", r.id}, {"at", e.fetchedAt}, {"b", e.build}, {"n", r.name}, {"ic", r.icon},
                  {"d", r.description}, {"f", facts}};
        if (type == CL::LINK_ITEM || type == CL::LINK_SKIN || type == CL::LINK_RECIPE) o["r"] = int(r.rarity);
        if (type == CL::LINK_ITEM) {
            o["bd"] = int(r.bound);
            o["ns"] = r.noSell;
            o["tr"] = r.tradeable;
            o["v"]  = r.vendorValue;
        }
        if (type == CL::LINK_RECIPE) {
            o["oi"] = r.outputItemId;
            o["oc"] = r.outputCount;
            o["mr"] = r.minRating;
        }
        list.push_back(std::move(o));
    }
    return json({{"version", kCacheVersion}, {"entries", list}}).dump();
}

bool Resolver::LoadCache(Lang lang, uint8_t type, const std::string& text)
{
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("version") || j["version"] != kCacheVersion ||
        !j.contains("entries") || !j["entries"].is_array())
        return false;   // unknown shape: start empty, entries are refetched
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        for (const auto& o : j["entries"]) {
            Record r;
            r.type = type;
            r.lang = lang;
            r.id   = o.value("id", 0u);
            r.name = o.value("n", std::string());
            if (r.id == 0 || r.name.empty()) continue;
            r.icon         = o.value("ic", std::string());
            r.description  = o.value("d", std::string());
            r.rarity       = Rarity(o.value("r", 0));
            r.bound        = Bound(o.value("bd", 0));
            r.noSell       = o.value("ns", false);
            r.tradeable    = o.value("tr", false);
            r.vendorValue  = o.value("v", 0);
            r.outputItemId = o.value("oi", 0u);
            r.outputCount  = o.value("oc", uint16_t(0));
            r.minRating    = o.value("mr", uint16_t(0));
            if (o.contains("f") && o["f"].is_array())
                for (const auto& f : o["f"])
                    if (f.is_array() && f.size() == 2 && f[0].is_string() && f[1].is_string())
                        r.facts.push_back({f[0].get<std::string>(), f[1].get<std::string>()});
            Entry& e = entries_[Key(type, lang, r.id)];
            if (e.pending) continue;                       // a fresher answer is on its way
            e.record    = std::move(r);
            e.warm      = true;
            e.fetchedAt = o.value("at", int64_t(0));
            e.build     = o.value("b", 0u);
        }
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::pair<Lang, uint8_t>> Resolver::TakeDirty()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<Lang, uint8_t>> out;
    for (const auto& d : dirty_) out.push_back({Lang(d.first), uint8_t(d.second)});
    dirty_.clear();
    return out;
}

void Resolver::ClearCache()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.pending) { it->second.warm = false; ++it; }
        else it = entries_.erase(it);
    }
    for (Lang l : {Lang::En, Lang::De})
        for (uint8_t t : {CL::LINK_ITEM, CL::LINK_SKIN, CL::LINK_SKILL, CL::LINK_TRAIT, CL::LINK_RECIPE})
            dirty_.insert({int(l), int(t)});
}

}} // namespace Trailpack::Core
