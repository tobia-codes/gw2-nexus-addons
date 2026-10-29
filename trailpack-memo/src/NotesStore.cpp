#include "NotesStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
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

static bool isString(const nlohmann::json& obj, const char* key)
{
    return obj.contains(key) && obj[key].is_string();
}

static const char* const kNotesFormat   = "trailpack-memo-notes";
static const char* const kExportFormat  = "trailpack-memo-export";
static const int         kFormatVersion = 2;

// ── NotesStore ────────────────────────────────────────────────────────────────

std::string NotesStore::newId_() const
{
    std::string id;
    do { id = makeId(); } while (
        std::any_of(notes_.begin(), notes_.end(), [&](const Note& n) { return n.id == id; }) ||
        std::any_of(groups_.begin(), groups_.end(), [&](const Group& g) { return g.id == id; }));
    return id;
}

const std::vector<Note>& NotesStore::Notes() const
{
    return notes_;
}

Note& NotesStore::Create(const std::string& defaultTitle, const std::string& groupId)
{
    Note n;
    n.id      = newId_();
    n.title   = defaultTitle;
    n.groupId = GetGroup(groupId) != nullptr ? groupId : std::string();
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

// ── Groups ────────────────────────────────────────────────────────────────────

const std::vector<Group>& NotesStore::Groups() const
{
    return groups_;
}

Group& NotesStore::CreateGroup(const std::string& name)
{
    Group g;
    g.id   = newId_();
    g.name = name;
    groups_.push_back(std::move(g));
    return groups_.back();
}

Group* NotesStore::GetGroup(const std::string& id)
{
    if (id.empty()) return nullptr;
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const Group& g) { return g.id == id; });
    return it != groups_.end() ? &(*it) : nullptr;
}

void NotesStore::DeleteGroup(const std::string& id)
{
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const Group& g) { return g.id == id; });
    if (it == groups_.end()) return;
    for (auto& n : notes_)
        if (n.groupId == id) n.groupId.clear();
    groups_.erase(it);
}

bool NotesStore::MoveToGroup(const std::string& noteId, const std::string& groupId)
{
    Note* n = Get(noteId);
    if (n == nullptr) return false;
    if (!groupId.empty() && GetGroup(groupId) == nullptr) return false;
    n->groupId = groupId;
    return true;
}

// ── Persistence ───────────────────────────────────────────────────────────────

std::string NotesStore::ToJson() const
{
    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : groups_)
        groups.push_back({{"id", g.id}, {"name", g.name}, {"collapsed", g.collapsed}});
    nlohmann::json notes = nlohmann::json::array();
    for (const auto& n : notes_)
        notes.push_back({{"id", n.id}, {"title", n.title}, {"body", n.body}, {"group", n.groupId}});
    nlohmann::json doc = {{"format", kNotesFormat}, {"version", kFormatVersion},
                          {"groups", groups}, {"notes", notes}};
    return doc.dump();
}

bool NotesStore::FromJson(const std::string& json)
{
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return false;
    }

    // Version 2 object, or the version-1 plain array of notes.
    const nlohmann::json* noteArr  = &parsed;
    const nlohmann::json* groupArr = nullptr;
    if (parsed.is_object()) {
        if (!parsed.contains("format") || parsed["format"] != kNotesFormat) return false;
        if (!parsed.contains("notes")) return false;
        noteArr = &parsed["notes"];
        if (parsed.contains("groups")) {
            groupArr = &parsed["groups"];
            if (!groupArr->is_array()) return false;
        }
    }
    if (!noteArr->is_array()) return false;

    std::vector<Group> groups;
    if (groupArr != nullptr) {
        for (const auto& obj : *groupArr) {
            if (!obj.is_object() || !isString(obj, "id") || !isString(obj, "name")) return false;
            Group g;
            g.id        = obj["id"].get<std::string>();
            g.name      = obj["name"].get<std::string>();
            g.collapsed = obj.contains("collapsed") && obj["collapsed"].is_boolean() &&
                          obj["collapsed"].get<bool>();
            groups.push_back(std::move(g));
        }
    }

    std::vector<Note> tmp;
    tmp.reserve(noteArr->size());
    for (const auto& obj : *noteArr) {
        if (!obj.is_object()) return false;
        if (!isString(obj, "id") || !isString(obj, "title") || !isString(obj, "body")) return false;
        Note n;
        n.id    = obj["id"].get<std::string>();
        n.title = obj["title"].get<std::string>();
        n.body  = obj["body"].get<std::string>();
        if (isString(obj, "group")) n.groupId = obj["group"].get<std::string>();
        tmp.push_back(std::move(n));
    }

    // Migrate legacy (counter) ids and repair repeats so every id is globally unique.
    // Groups first: notes reference them, and only a group's original valid id can match.
    std::set<std::string> seen;
    std::set<std::string> validGroups;
    for (auto& g : groups) {
        if (!IsNoteId(g.id) || seen.count(g.id)) {
            do { g.id = makeId(); } while (seen.count(g.id));
        } else {
            validGroups.insert(g.id);
        }
        seen.insert(g.id);
    }
    for (auto& n : tmp) {
        if (!IsNoteId(n.id) || seen.count(n.id)) {
            do { n.id = makeId(); } while (seen.count(n.id));
        }
        seen.insert(n.id);
        if (!validGroups.count(n.groupId)) n.groupId.clear();
    }

    notes_  = std::move(tmp);
    groups_ = std::move(groups);
    return true;
}

// ── Export / import ───────────────────────────────────────────────────────────

static nlohmann::json exportDoc(const std::vector<const Group*>& groups,
                                const std::vector<const Note*>& notes)
{
    nlohmann::json g = nlohmann::json::array();
    for (const Group* grp : groups)
        g.push_back({{"id", grp->id}, {"name", grp->name}});
    nlohmann::json n = nlohmann::json::array();
    for (const Note* note : notes)
        n.push_back({{"id", note->id}, {"title", note->title}, {"body", note->body},
                     {"group", note->groupId}});
    return {{"format", kExportFormat}, {"version", kFormatVersion}, {"groups", g}, {"notes", n}};
}

std::string NotesStore::ExportJson(const std::vector<std::string>& ids) const
{
    std::vector<const Note*> notes;
    std::set<std::string>    usedGroups;
    for (const auto& n : notes_) {
        if (!ids.empty() && std::find(ids.begin(), ids.end(), n.id) == ids.end())
            continue;
        notes.push_back(&n);
        if (!n.groupId.empty()) usedGroups.insert(n.groupId);
    }
    std::vector<const Group*> groups;
    for (const auto& g : groups_)
        if (ids.empty() || usedGroups.count(g.id)) groups.push_back(&g);
    return exportDoc(groups, notes).dump(2);
}

std::string NotesStore::ExportGroupJson(const std::string& groupId) const
{
    std::vector<const Group*> groups;
    for (const auto& g : groups_)
        if (g.id == groupId) groups.push_back(&g);
    std::vector<const Note*> notes;
    if (!groups.empty())
        for (const auto& n : notes_)
            if (n.groupId == groupId) notes.push_back(&n);
    return exportDoc(groups, notes).dump(2);
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

    // Export file / notes.json -> their "notes" (+ "groups"); a bare array is a v1 notes.json.
    const nlohmann::json* arr = &parsed;
    std::map<std::string, std::string> groupNames;   // valid group id -> name
    if (parsed.is_object()) {
        if (!parsed.contains("format") ||
            (parsed["format"] != kExportFormat && parsed["format"] != kNotesFormat)) return false;
        if (!parsed.contains("notes")) return false;
        arr = &parsed["notes"];
        if (parsed.contains("groups")) {
            const nlohmann::json& groups = parsed["groups"];
            if (!groups.is_array()) return false;
            for (const auto& g : groups) {
                if (!g.is_object() || !isString(g, "id") || !isString(g, "name")) return false;
                const std::string id = g["id"].get<std::string>();
                if (IsNoteId(id)) groupNames.emplace(id, g["name"].get<std::string>());
            }
        }
    }
    if (!arr->is_array()) return false;

    std::vector<ImportItem> tmp;
    tmp.reserve(arr->size());
    for (const auto& obj : *arr) {
        if (!obj.is_object())                                   return false;
        if (!isString(obj, "title") || !isString(obj, "body")) return false;
        ImportItem it;
        // Only globally unique ids identify a note; legacy counter ids are ignored.
        if (isString(obj, "id") && IsNoteId(obj["id"].get<std::string>()))
            it.id = obj["id"].get<std::string>();
        it.title = obj["title"].get<std::string>();
        it.body  = obj["body"].get<std::string>();
        if (isString(obj, "group")) {
            const auto g = groupNames.find(obj["group"].get<std::string>());
            if (g != groupNames.end()) { it.groupId = g->first; it.groupName = g->second; }
        }

        const bool repeat = std::any_of(tmp.begin(), tmp.end(), [&](const ImportItem& prev) {
            return it.id.empty() ? (prev.id.empty() && prev.title == it.title && prev.body == it.body)
                                 : prev.id == it.id;
        });
        it.duplicate = repeat || hasDuplicateOf_(it);
        if (!repeat && !it.id.empty()) {
            const auto local = std::find_if(notes_.begin(), notes_.end(),
                                            [&](const Note& n) { return n.id == it.id; });
            it.changed = local != notes_.end() &&
                         (local->title != it.title || local->body != it.body);
        }
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

        // Reuse the group by id, or recreate it with the file's id + name. An id already
        // taken by a note can't become a group -> the note is imported ungrouped.
        std::string groupId;
        if (IsNoteId(item.groupId)) {
            if (GetGroup(item.groupId) == nullptr && Get(item.groupId) == nullptr) {
                Group g;
                g.id   = item.groupId;
                g.name = item.groupName;
                groups_.push_back(std::move(g));
            }
            if (GetGroup(item.groupId) != nullptr) groupId = item.groupId;
        }

        Note& n = Create(item.title, groupId);
        if (IsNoteId(item.id) && GetGroup(item.id) == nullptr) n.id = item.id;
        n.body = item.body;
        ++added;
    }
    return added;
}

int NotesStore::UpdateImported(const std::vector<ImportItem>& items)
{
    int updated = 0;
    for (const auto& item : items) {
        Note* n = IsNoteId(item.id) ? Get(item.id) : nullptr;
        if (n == nullptr || (n->title == item.title && n->body == item.body)) continue;
        n->title = item.title;
        n->body  = item.body;
        ++updated;
    }
    return updated;
}

int NotesStore::ImportJson(const std::string& json)
{
    std::vector<ImportItem> items;
    if (!PrepareImport(json, items)) return -1;
    return AddImported(items);
}

} // namespace Magpie
