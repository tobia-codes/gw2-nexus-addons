#include "NotesStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <random>
#include <set>

namespace Magpie {

// ── Helpers ───────────────────────────────────────────────────────────────────

// Random 128-bit id as 32 lowercase hex chars.
static std::string makeId()
{
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    static const char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int half = 0; half < 2; ++half) {
        unsigned long long v = rng();
        for (int i = 0; i < 16; ++i, v >>= 4) id += hex[v & 0xF];
    }
    return id;
}

bool IsNoteId(const std::string& id)
{
    if (id.size() != 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// ── NotesStore ────────────────────────────────────────────────────────────────

const std::vector<Note>& NotesStore::Notes() const
{
    return notes_;
}

Note& NotesStore::Create(const std::string& defaultTitle)
{
    Note n;
    do { n.id = makeId(); } while (Get(n.id) != nullptr);
    n.title = defaultTitle;
    notes_.push_back(std::move(n));
    return notes_.back();
}

void NotesStore::Delete(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(),
                           [&](const Note& n) { return n.id == id; });
    if (it != notes_.end())
        notes_.erase(it);
}

Note* NotesStore::Get(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(),
                           [&](const Note& n) { return n.id == id; });
    return it != notes_.end() ? &(*it) : nullptr;
}

std::string NotesStore::ToJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : notes_)
        arr.push_back({{"id", n.id}, {"title", n.title}, {"body", n.body}});
    return arr.dump();
}

bool NotesStore::FromJson(const std::string& json)
{
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return false;
    }

    if (!parsed.is_array())
        return false;

    std::vector<Note> tmp;
    tmp.reserve(parsed.size());
    for (const auto& obj : parsed) {
        if (!obj.is_object())                    return false;
        if (!obj.contains("id")    || !obj["id"].is_string())    return false;
        if (!obj.contains("title") || !obj["title"].is_string()) return false;
        if (!obj.contains("body")  || !obj["body"].is_string())  return false;
        Note n;
        n.id    = obj["id"].get<std::string>();
        n.title = obj["title"].get<std::string>();
        n.body  = obj["body"].get<std::string>();
        tmp.push_back(std::move(n));
    }

    // Migrate legacy (counter) ids and repair repeats so every id is globally unique.
    std::set<std::string> seen;
    for (auto& n : tmp) {
        if (!IsNoteId(n.id) || seen.count(n.id)) {
            do { n.id = makeId(); } while (seen.count(n.id));
        }
        seen.insert(n.id);
    }

    notes_ = std::move(tmp);
    return true;
}

// ── Export / import ───────────────────────────────────────────────────────────

static const char* const kExportFormat  = "trailpack-memo-export";
static const int         kExportVersion = 1;

std::string NotesStore::ExportJson(const std::vector<std::string>& ids) const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : notes_) {
        if (!ids.empty() && std::find(ids.begin(), ids.end(), n.id) == ids.end())
            continue;
        arr.push_back({{"id", n.id}, {"title", n.title}, {"body", n.body}});
    }
    nlohmann::json doc = {{"format", kExportFormat}, {"version", kExportVersion}, {"notes", arr}};
    return doc.dump(2);
}

bool NotesStore::hasDuplicateOf_(const ImportItem& item) const
{
    return std::any_of(notes_.begin(), notes_.end(), [&](const Note& n) {
        return IsNoteId(item.id) ? n.id == item.id
                                 : (n.title == item.title && n.body == item.body);
    });
}

bool NotesStore::PrepareImport(const std::string& json, std::vector<ImportItem>& out) const
{
    out.clear();
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return false;
    }

    // Export file -> its "notes" array; a bare array is a notes.json.
    const nlohmann::json* arr = &parsed;
    if (parsed.is_object()) {
        if (!parsed.contains("format") || parsed["format"] != kExportFormat) return false;
        if (!parsed.contains("notes")) return false;
        arr = &parsed["notes"];
    }
    if (!arr->is_array()) return false;

    std::vector<ImportItem> tmp;
    tmp.reserve(arr->size());
    for (const auto& obj : *arr) {
        if (!obj.is_object())                                    return false;
        if (!obj.contains("title") || !obj["title"].is_string()) return false;
        if (!obj.contains("body")  || !obj["body"].is_string())  return false;
        ImportItem it;
        // Only globally unique ids identify a note; legacy counter ids are ignored.
        if (obj.contains("id") && obj["id"].is_string() && IsNoteId(obj["id"].get<std::string>()))
            it.id = obj["id"].get<std::string>();
        it.title = obj["title"].get<std::string>();
        it.body  = obj["body"].get<std::string>();

        it.duplicate = hasDuplicateOf_(it) ||
            std::any_of(tmp.begin(), tmp.end(), [&](const ImportItem& prev) {
                return it.id.empty() ? (prev.id.empty() && prev.title == it.title && prev.body == it.body)
                                     : prev.id == it.id;
            });
        tmp.push_back(std::move(it));
    }

    out = std::move(tmp);
    return true;
}

int NotesStore::AddImported(const std::vector<ImportItem>& items)
{
    int added = 0;
    for (const auto& item : items) {
        if (hasDuplicateOf_(item)) continue;
        Note& n = Create(item.title);
        if (IsNoteId(item.id)) n.id = item.id;
        n.body = item.body;
        ++added;
    }
    return added;
}

int NotesStore::ImportJson(const std::string& json)
{
    std::vector<ImportItem> items;
    if (!PrepareImport(json, items)) return -1;
    return AddImported(items);
}

} // namespace Magpie
