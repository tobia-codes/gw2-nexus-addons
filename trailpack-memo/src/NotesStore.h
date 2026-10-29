#pragma once
#include <string>
#include <vector>

namespace Magpie {

struct Note {
    std::string id;     // globally unique: 32 lowercase hex chars (random 128 bit), kept on
                        // export/import so the same note is recognised on another install
    std::string title;
    std::string body;
    std::string groupId;  // id of the Group it belongs to; empty = ungrouped
};

// A named, collapsible group of notes (one level, no nesting). Ids use the same globally
// unique format as notes, so a group is recognised again on import.
struct Group {
    std::string id;
    std::string name;
    bool        collapsed = false;  // UI state; saved in notes.json, not exported
    bool        hasColor  = false;  // own name colour; false = the addon-wide group colour
    float       color[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };   // RGBA 0..1 (when hasColor)
};

// One note read from an import file (see NotesStore::PrepareImport).
struct ImportItem {
    std::string id;              // the note's id from the file; empty if it had no valid one
    std::string title;
    std::string body;
    bool        duplicate = false;  // already in the store (or earlier in the same file)
    bool        changed   = false;  // duplicate by id whose title/body differ from the local
                                    // note -> can be offered as an update (UpdateImported)
    std::string groupId;            // the note's group in the file (valid id), or empty
    std::string groupName;          // that group's name in the file
    bool        groupHasColor = false;  // that group's own colour in the file, if any
    float       groupColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// True if `id` has the globally unique format (32 lowercase hex chars). Used for note and
// group ids alike.
bool IsNoteId(const std::string& id);

class NotesStore {
public:
    const std::vector<Note>& Notes() const;

    Note& Create(const std::string& defaultTitle = "Untitled note",
                 const std::string& groupId = std::string());
    void  Delete(const std::string& id);
    // Delete every note whose id is in `ids` (unknown ids ignored). Returns the count removed.
    int   DeleteMany(const std::vector<std::string>& ids);
    Note* Get(const std::string& id);

    // ── Groups ────────────────────────────────────────────────────────────
    const std::vector<Group>& Groups() const;
    Group& CreateGroup(const std::string& name);
    Group* GetGroup(const std::string& id);
    // Remove the group; its notes are kept and become ungrouped.
    void   DeleteGroup(const std::string& id);
    // Put a note into a group ("" = ungroup). False if the note or group is unknown.
    bool   MoveToGroup(const std::string& noteId, const std::string& groupId);

    // notes.json:
    //   {"format":"trailpack-memo-notes","version":2,
    //    "groups":[{id,name,collapsed,color},...],"notes":[{id,title,body,group},...]}
    //   (color = [r,g,b,a] or null)
    std::string ToJson() const;
    // Replace all notes and groups. Accepts the format above or the version-1 plain array
    // of notes (older Trailpack / Magpie Memo). Ids that are missing, not in IsNoteId format
    // (counters like "00000000") or repeated are replaced with fresh ones -- the caller
    // should save afterwards so the new ids stay stable. Unknown group refs -> ungrouped.
    bool        FromJson(const std::string& json);

    // Export/import for sharing and backups. Export file:
    //   {"format":"trailpack-memo-export","version":2,
    //    "groups":[{id,name,color},...],"notes":[{id,title,body,group},...]}
    // `ids` empty -> every note and every group (incl. empty ones); otherwise just those
    // notes, plus the groups they belong to. Unknown ids are skipped.
    std::string ExportJson(const std::vector<std::string>& ids = {}) const;
    // One group and all of its notes.
    std::string ExportGroupJson(const std::string& groupId) const;

    // Read an export file (version 1 or 2), a notes.json (either version) or a plain
    // version-1 array (Trailpack / Magpie Memo) into `out` without changing the store. Each item is flagged `duplicate` when a note with
    // its id already exists; items without a valid id fall back to an identical title+body
    // match. Returns false (out empty) if the input is not a valid notes file.
    bool        PrepareImport(const std::string& json, std::vector<ImportItem>& out) const;
    // Append `items` as new notes, skipping duplicates (re-checked against the store). An
    // item keeps its id when it has one; otherwise it gets a fresh id. An item's group is
    // reused when a group with that id exists (keeping its local name/colour), else created
    // with the file's id, name and colour.
    // Returns the count added.
    int         AddImported(const std::vector<ImportItem>& items);
    // Overwrite title + body of the local notes that share an item's id (the "changed"
    // duplicates the user chose to update). Items without a matching note are ignored.
    // Returns the number of notes actually changed.
    int         UpdateImported(const std::vector<ImportItem>& items);
    // PrepareImport + AddImported of every item. Returns the count added, or -1 if invalid.
    int         ImportJson(const std::string& json);

private:
    std::vector<Note>  notes_;
    std::vector<Group> groups_;

    std::string newId_() const;   // fresh id unused by any note or group

    bool hasDuplicateOf_(const ImportItem& item) const;
};

} // namespace Magpie
