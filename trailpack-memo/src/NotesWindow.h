#pragma once
// NotesWindow.h — Two-pane notes UI + JSON persistence for Trailpack Memo.

#include <string>
#include <filesystem>
#include <future>
#include "nexus/Nexus.h"
#include "NotesStore.h"
#include "ChipTextEdit.h"

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
    // ── Mode ──────────────────────────────────────────────────────────────
    enum class Mode { View, Edit };

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
    bool settingsChanged_ = false;

    // ── Dirty-guard pending navigation ────────────────────────────────────
    // When a navigation is requested while dirty we stash it here and open
    // the "Unsaved Changes" modal.  Both fields are cleared after resolution.
    std::string pendingSelectId_;  // non-empty => switch to this note after save/discard
    bool        pendingCreate_ = false;  // true => create new note after save/discard
    std::string pendingCreateGroupId_;   // group for that new note ("" = ungrouped)

    // ── Export / import ───────────────────────────────────────────────────
    // The Windows file dialog runs on a worker thread so the game keeps rendering;
    // Render() polls the result and performs the file I/O on the render thread.
    enum class IoAction { None, Export, Import };
    std::future<std::wstring> ioDialog_;      // valid while a dialog is open
    IoAction                  ioAction_ = IoAction::None;
    std::string               ioExportId_;    // Export: single note id; empty = all notes
    std::string               ioExportGroupId_;  // Export: whole group (takes precedence)
    std::string               ioMessage_;     // result shown in the "Import / Export" modal

    // Import selection: the notes read from the chosen file, awaiting the user's pick.
    std::vector<Magpie::ImportItem> importItems_;
    std::vector<char>               importChecked_;   // parallel to importItems_
    std::string                     importFile_;      // file name shown in the modal

    // ── Delete-confirmation ───────────────────────────────────────────────
    std::string pendingDeleteId_;  // non-empty => the note awaiting delete confirmation

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
    std::vector<std::string> bulkSelected_;      // ids ticked in the modal
    bool                     bulkConfirm_ = false;  // second step: "really delete?"

    // ── Private helpers ───────────────────────────────────────────────────
    void selectFirst_();
    void advanceSelectionAfterDelete_(const std::string& deletedId);

    // Enter edit mode for the currently selected note (seeds buffers).
    void enterEdit_(Magpie::Note* note);

    // Commit edit buffers to store and persist.
    void commitEdit_();

    // Discard edit buffers and return to View.
    void cancelEdit_();

    // Perform the stashed pending navigation after save/discard.
    void resolvePending_();

    // Request navigation: intercepts with modal if dirty, else navigates directly.
    void requestSelect_(const std::string& id);
    // Create a note (in `groupId`, "" = ungrouped); asks first if the edit is dirty.
    void requestCreate_(const std::string& groupId = std::string());

    // Draw the dirty-state modal.  Returns true when open (caller must not
    // do further navigation this frame).
    void renderUnsavedModal_();

    // Draw the delete-confirmation modal (acts on pendingDeleteId_).
    void renderDeleteModal_();

    // Perform the actual deletion of `id` (handles edit-mode + selection).
    void performDelete_(const std::string& id);

    // Open the save dialog for one note (`id`) or all notes (empty id).
    void startExport_(const std::string& id);
    // Open the save dialog for one group and its notes.
    void startExportGroup_(const std::string& groupId);
    // Open the open-file dialog; the chosen file's notes are appended.
    void startImport_();
    // Finish a completed dialog (write/read the file) and open the result modal.
    void pollFileDialog_();
    // Draw the "Import / Export" result modal.
    void renderIoModal_();
    // Draw the "Import Notes" selection modal (one checkbox per note in the file).
    void renderImportModal_();

    // Note list: one note row (selectable, progress, context menu, drag source).
    // `indent` shifts the title for notes inside a group.
    void renderNoteRow_(const Magpie::Note& note, float indent, std::string& clickedId);
    // Note list: a group header (collapse toggle, count, context menu, drop target).
    // Returns whether the group is expanded.
    // Popups that must open in the window scope are reported through the out params.
    bool renderGroupHeader_(Magpie::Group& group, bool& openNameModal, bool& openDeleteModal,
                            bool& openColorModal, std::string& createInGroup);
    // A group's name colour: its own, else the addon-wide group colour, else the border.
    ImVec4 groupColor_(const Magpie::Group& group) const;
    void   renderGroupColorModal_();
    // Group-name modal (create / rename) and delete-group confirmation.
    void renderGroupNameModal_();
    void renderDeleteGroupModal_();
    // "Delete notes" modal: tick notes (per group, Select all / None), then confirm.
    void renderBulkDeleteModal_();

    // Nexus's FontBig (for headings), or null.
    ImFont* fontBig_() const;

    // Markdown toolbar row above the body editor (B / I / H1 / H2 / list / task + Preview).
    void renderEditToolbar_();

    // View-mode body: rendered markdown with clickable task checkboxes + context menu.
    void renderViewBody_(Magpie::Note* note);
};
