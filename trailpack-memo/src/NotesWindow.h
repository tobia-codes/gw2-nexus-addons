#pragma once
// NotesWindow.h — Two-pane notes UI + JSON persistence for Trailpack Memo.
//
// The implementation is split by area:
//   NotesWindow.cpp        load/save, navigation, frame layout, popup requests, caches
//   NotesList.cpp          left column: groups, note rows, menu button, splitter
//   NotesDetail.cpp        right pane: view / edit modes, toolbar, rendered body
//   NotesModals.cpp        confirmations and group / bulk-delete / colour modals
//   NotesImportExport.cpp  file dialogs, export, import selection
// Small shared UI helpers live in NotesUi.h.

#include <string>
#include <filesystem>
#include <future>
#include <unordered_map>
#include <vector>
#include "nexus/Nexus.h"
#include "NotesStore.h"
#include "ChipTextEdit.h"
#include "Markdown.h"

class NotesWindow {
public:
    void Init(AddonAPI_t* api);
    void Render();
    void Save();
    void Shutdown();

    // Side-by-side live preview while editing (persisted by dllmain's settings.json).
    bool PreviewEnabled() const     { return showPreview_; }
    void SetPreviewEnabled(bool on) { showPreview_ = on; }

    // Note text size (view, editor, preview) as a factor of Nexus's base font size,
    // independent of the window's UI text size (set by dllmain from settings.json).
    // Applied with Magpie::FontScaleScope (FontScale.h).
    void SetNoteTextScale(float scale) { noteTextScale_ = scale; }

    // Width of the note list in px (drag the splitter; persisted by dllmain's settings.json).
    static constexpr float kDefaultListWidth = 200.0f;
    float ListWidth() const         { return listWidth_; }
    void  SetListWidth(float w)     { listWidth_ = w; }
    // True once after the user changed a persisted UI setting (caller saves settings).
    bool ConsumeSettingsChanged()   { bool c = settingsChanged_; settingsChanged_ = false; return c; }

private:
    enum class Mode { View, Edit };

    // ── Popups ────────────────────────────────────────────────────────────
    // Modals are opened through requestPopup_ from anywhere (list child, context menus,
    // dialogs) and actually opened at the end of Render, in the window's own ID scope where
    // their BeginPopupModal calls live.
    enum class Popup : unsigned {
        Unsaved, ConfirmDelete, Notice, IoResult, ImportNotes,
        GroupName, DeleteGroup, GroupColor, BulkDelete, Count
    };
    unsigned popupRequests_ = 0;   // bit per Popup
    void requestPopup_(Popup p) { popupRequests_ |= 1u << (unsigned)p; }
    void openRequestedPopups_();
    // Centred, auto-sized modal with its translated title; false when not open.
    bool beginModal_(Popup p);

    // ── Persistent state ──────────────────────────────────────────────────
    AddonAPI_t*           api_ = nullptr;  // Nexus API (for FontBig via DataLink)
    Magpie::NotesStore    store_;
    std::string           selectedId_;
    std::filesystem::path notesFile_;
    std::filesystem::path exportDir_;   // default folder for export/import dialogs

    // ── Edit-mode working copies ──────────────────────────────────────────
    Mode                   mode_        = Mode::View;
    Magpie::ChipTextEdit   bodyEditor_;  // working copy of note body while editing
    std::string            titleBuffer_; // working copy of note title while editing

    // ── UI settings ───────────────────────────────────────────────────────
    bool  showPreview_     = false;
    float noteTextScale_   = 1.0f;
    float listWidth_       = kDefaultListWidth;
    bool  settingsChanged_ = false;

    // ── Navigation requests ───────────────────────────────────────────────
    // When a navigation is requested while the edit is dirty it is stashed here and the
    // "Unsaved Changes" modal opens; resolvePending_ carries it out after save/discard.
    std::string pendingSelectId_;        // switch to this note after save/discard
    bool        pendingCreate_ = false;  // create a new note after save/discard
    std::string pendingCreateGroupId_;   // group for that new note ("" = ungrouped)
    std::string pendingEditId_;          // open this note in the editor once selected

    // Picked this frame in the list / context menus; handled at the end of Render.
    std::string selectRequestId_;
    std::string editRequestId_;
    std::string deleteRequestId_;
    bool        createRequested_ = false;
    std::string createRequestGroupId_;

    std::string pendingDeleteId_;        // the note awaiting delete confirmation

    // ── Export / import ───────────────────────────────────────────────────
    // The Windows file dialog runs on a worker thread so the game keeps rendering;
    // Render() polls the result and performs the file I/O on the render thread.
    enum class IoAction { None, Export, Import };
    std::future<std::wstring> ioDialog_;         // valid while a dialog is open
    IoAction                  ioAction_ = IoAction::None;
    std::string               ioExportId_;       // Export: single note id; empty = all notes
    std::string               ioExportGroupId_;  // Export: whole group (takes precedence)
    std::string               ioMessage_;        // result shown in the "Import / Export" modal

    // Import selection: the notes read from the chosen file, awaiting the user's pick.
    std::vector<Magpie::ImportItem> importItems_;
    std::vector<char>               importChecked_;   // parallel to importItems_
    std::string                     importFile_;      // file name shown in the modal

    // Load problem (unreadable notes.json), told once in the "Notice" modal.
    std::string noticeMessage_;

    // ── Groups ────────────────────────────────────────────────────────────
    // Group-name modal: create (groupNameId_ empty) or rename (groupNameId_ = group).
    std::string groupNameId_;
    std::string groupNameBuffer_;
    std::string pendingDeleteGroupId_;   // group awaiting delete confirmation
    // Group-colour modal: the group being edited + its colour before (for Cancel).
    std::string groupColorId_;
    bool        groupColorHadOwn_ = false;
    float       groupColorBefore_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // ── Delete several notes ("Delete notes..." modal) ────────────────────
    std::vector<std::string> bulkSelected_;         // ids ticked in the modal
    bool                     bulkConfirm_ = false;  // second step: "really delete?"

    // ── Per-frame caches ──────────────────────────────────────────────────
    // Markdown parse of a body, redone only when the text changed (view / preview).
    struct ParseCache {
        bool                          valid = false;
        std::string                   body;
        std::vector<Magpie::Md::Line> lines;
    };
    ParseCache viewParse_, previewParse_;
    // Task progress per note id, keyed by a hash of the body.
    struct TaskCache { size_t bodyHash = 0; Magpie::Md::TaskCount count; };
    std::unordered_map<std::string, TaskCache> taskCache_;

    const std::vector<Magpie::Md::Line>& parsed_(ParseCache& cache, const std::string& body);
    Magpie::Md::TaskCount taskCount_(const Magpie::Note& note);

    // ── NotesWindow.cpp: state + navigation ───────────────────────────────
    // Load notes.json; an unreadable file is set aside (never overwritten) and the last
    // backup (notes.json.bak) is tried instead.
    void loadNotes_();
    void selectFirst_();
    void advanceSelectionAfterDelete_(const std::string& deletedId);
    void enterEdit_(Magpie::Note* note);   // seed the edit buffers, switch to Edit
    void commitEdit_();                     // write the buffers back and save
    void cancelEdit_();                     // drop the buffers, back to View
    bool editIsDirty_();
    // Navigation that may need the unsaved-changes modal first.
    void requestSelect_(const std::string& id);
    void requestCreate_(const std::string& groupId = std::string());
    void resolvePending_();                 // carry out the stashed navigation
    void handleRequests_();                 // the *Request* fields above, end of Render
    void performDelete_(const std::string& id);
    ImFont* fontBig_() const;               // Nexus's FontBig (headings), or null

    // ── NotesList.cpp ─────────────────────────────────────────────────────
    // Left column (list, "Create New Note", menu button); returns its width.
    float renderListColumn_();
    void  renderSplitter_(float listWidth);
    // One note row (selectable, badge, progress, context menu, drag source).
    void  renderNoteRow_(const Magpie::Note& note, float indent);
    // A group header (collapse toggle, count, context menu, drop target); true if expanded.
    bool  renderGroupHeader_(Magpie::Group& group);
    void  typeMenu_(const std::string& noteId);   // "Type" submenu: Note / Daily / Weekly
    // A group's name colour: its own, else the addon-wide group colour, else the border.
    ImVec4 groupColor_(const Magpie::Group& group) const;

    // ── NotesDetail.cpp ───────────────────────────────────────────────────
    void renderDetailPane_();
    void renderEditMode_(Magpie::Note& note);
    void renderViewMode_(Magpie::Note& note);
    void renderEditToolbar_();
    void renderViewBody_(Magpie::Note* note);

    // ── NotesModals.cpp ───────────────────────────────────────────────────
    void renderModals_();
    void renderUnsavedModal_();
    void renderDeleteModal_();
    void renderNoticeModal_();
    void renderGroupNameModal_();
    void renderDeleteGroupModal_();
    void renderGroupColorModal_();
    void renderBulkDeleteModal_();

    // ── NotesImportExport.cpp ─────────────────────────────────────────────
    void startExport_(const std::string& id);        // one note (`id`) or all (empty)
    void startExportGroup_(const std::string& groupId);
    void startImport_();
    void pollFileDialog_();                           // finish a completed dialog
    void renderIoModal_();
    void renderImportModal_();
};
