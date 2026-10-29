#pragma once
#include <string>
#include <vector>

namespace Magpie {

struct Note {
    std::string id;     // globally unique: 32 lowercase hex chars (random 128 bit), kept on
                        // export/import so the same note is recognised on another install
    std::string title;
    std::string body;
};

// One note read from an import file (see NotesStore::PrepareImport).
struct ImportItem {
    std::string id;              // the note's id from the file; empty if it had no valid one
    std::string title;
    std::string body;
    bool        duplicate = false;  // already in the store (or earlier in the same file)
    bool        changed   = false;  // duplicate by id whose title/body differ from the local
                                    // note -> can be offered as an update (UpdateImported)
};

// True if `id` has the globally unique format (32 lowercase hex chars).
bool IsNoteId(const std::string& id);

class NotesStore {
public:
    const std::vector<Note>& Notes() const;

    Note& Create(const std::string& defaultTitle = "Untitled note");
    void  Delete(const std::string& id);
    Note* Get(const std::string& id);

    std::string ToJson() const;
    // Replace all notes. Ids that are missing, not in IsNoteId format (Magpie Memo / older
    // Trailpack counters like "00000000") or repeated are replaced with fresh ones -- the
    // caller should save afterwards so the new ids stay stable.
    bool        FromJson(const std::string& json);

    // Export/import for sharing and backups. Export file:
    //   {"format":"trailpack-memo-export","version":1,"notes":[{id,title,body},...]}
    // `ids` empty -> every note, in list order; unknown ids are skipped.
    std::string ExportJson(const std::vector<std::string>& ids = {}) const;

    // Read an export file -- or a plain notes.json array (Trailpack / Magpie Memo) -- into
    // `out` without changing the store. Each item is flagged `duplicate` when a note with
    // its id already exists; items without a valid id fall back to an identical title+body
    // match. Returns false (out empty) if the input is not a valid notes file.
    bool        PrepareImport(const std::string& json, std::vector<ImportItem>& out) const;
    // Append `items` as new notes, skipping duplicates (re-checked against the store). An
    // item keeps its id when it has one; otherwise it gets a fresh id. Returns the count added.
    int         AddImported(const std::vector<ImportItem>& items);
    // Overwrite title + body of the local notes that share an item's id (the "changed"
    // duplicates the user chose to update). Items without a matching note are ignored.
    // Returns the number of notes actually changed.
    int         UpdateImported(const std::vector<ImportItem>& items);
    // PrepareImport + AddImported of every item. Returns the count added, or -1 if invalid.
    int         ImportJson(const std::string& json);

private:
    std::vector<Note> notes_;

    bool hasDuplicateOf_(const ImportItem& item) const;
};

} // namespace Magpie
