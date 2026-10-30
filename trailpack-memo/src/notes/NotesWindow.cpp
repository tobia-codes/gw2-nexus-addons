// NotesWindow.cpp — note state, persistence, navigation and the frame layout.
// (List, detail pane, modals and import/export live in the other NotesWindow*.cpp files.)

#include "notes/NotesWindow.h"

#include <cstdio>
#include <ctime>
#include "imgui.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include "tpfiles/SafeFile.h"

using Trailpack::I18n::Tr;

// ── Popups ────────────────────────────────────────────────────────────────

// Translated title + stable id ("Title###id"): a popup keeps its ImGui id when the
// language changes, so OpenPopup and BeginPopupModal always agree.
static std::string PopupTitle(unsigned p)
{
    static const char* const kTitles[][2] = {
        { "Unsaved Changes", "unsaved" },        { "Confirm Delete", "confirm_delete" },
        { "Notice", "notice" },                  { "Import / Export", "import_export" },
        { "Import Notes", "import_notes" },      { "Group", "group_name" },
        { "Delete Group", "delete_group" },      { "Group Colour", "group_color" },
        { "Delete Notes", "bulk_delete" },
    };
    static_assert(sizeof kTitles / sizeof kTitles[0] == 9, "one title per Popup");
    return std::string(Tr(kTitles[p][0])) + "###" + kTitles[p][1];
}

void NotesWindow::openRequestedPopups_()
{
    for (unsigned p = 0; p < (unsigned)Popup::Count; ++p)
        if (popupRequests_ & (1u << p)) ImGui::OpenPopup(PopupTitle(p).c_str());
    popupRequests_ = 0;
}

bool NotesWindow::beginModal_(Popup p)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(PopupTitle((unsigned)p).c_str(), nullptr,
                                  ImGuiWindowFlags_AlwaysAutoResize);
}

// ── Init ──────────────────────────────────────────────────────────────────

void NotesWindow::Init(AddonAPI_t* api)
{
    api_ = api;

    const char* dir = api->Paths_GetAddonDirectory("TrailpackMemo");
    if (dir && dir[0] != '\0') {
        const std::string addonDir(dir);
        Trailpack::SafeFile::CreateDirectories(addonDir);   // no-op if already exists
        notesFile_ = Trailpack::SafeFile::Join(addonDir, "notes.json");
        exportDir_ = Trailpack::SafeFile::Join(addonDir, "exports");
    }

    loadNotes_();
    selectFirst_();
}

void NotesWindow::loadNotes_()
{
    if (notesFile_.empty() || !Trailpack::SafeFile::Exists(notesFile_)) return;

    std::string content;
    if (Trailpack::SafeFile::Read(notesFile_, content) && store_.FromJson(content)) {
        // First load of a version-1 file (plain array): keep a copy before it is
        // rewritten in the version-2 format, which older builds can't read.
        const std::string v1Backup = Trailpack::SafeFile::Join(DataDir(), "notes.v1-backup.json");
        const size_t first = content.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && content[first] == '[' && !Trailpack::SafeFile::Exists(v1Backup))
            Trailpack::SafeFile::WriteAtomic(v1Backup, content);
        // Save at once: loading may have replaced legacy ids, and exported ids must stay
        // stable across sessions.
        Save();
        return;
    }

    // Unreadable: never overwrite it. Move it aside, then fall back to the last backup.
    const std::string aside = Trailpack::SafeFile::SetAside(notesFile_);
    const std::string asideName = aside.empty() ? std::string("notes.json") : Trailpack::SafeFile::FileName(aside);
    std::string backup;
    const bool restored = Trailpack::SafeFile::Read(Trailpack::SafeFile::BackupPath(notesFile_), backup) &&
                          store_.FromJson(backup);
    char msg[512];
    snprintf(msg, sizeof msg,
             restored ? Tr("notes.json could not be read and was kept as %s.\n"
                           "Your notes were restored from the last backup.")
                      : Tr("notes.json could not be read and was kept as %s.\n"
                           "No backup could be loaded, so the list starts empty."),
             asideName.c_str());
    noticeMessage_ = msg;
    requestPopup_(Popup::Notice);   // opened with the first frame
    if (restored) Save();
}

// ── Save / Shutdown ───────────────────────────────────────────────────────

void NotesWindow::Save()
{
    if (notesFile_.empty()) return;
    // Crash-safe: temp file + atomic replace; the previous version stays as notes.json.bak.
    Trailpack::SafeFile::WriteAtomic(notesFile_, store_.ToJson(), /*keepBackup=*/true);
}

void NotesWindow::Shutdown()
{
    // A file dialog still open on its worker thread must finish before the DLL goes
    // away; this blocks the unload until the user closes it (the dialog stays visible).
    if (ioDialog_.valid()) ioDialog_.wait();
    Save();
}

bool NotesWindow::RestoreNotes(const std::string& json)
{
    Trailpack::NotesStore restored;
    if (!restored.FromJson(json)) return false;
    store_ = std::move(restored);

    cancelEdit_();
    pendingSelectId_.clear();
    pendingCreate_ = false;
    pendingEditId_.clear();
    pendingDeleteId_.clear();
    pendingDeleteGroupId_.clear();
    groupColorId_.clear();
    bulkSelected_.clear();
    viewParse_.valid = previewParse_.valid = false;
    taskCache_.clear();
    selectFirst_();
    Save();
    return true;
}

// ── Internal helpers ──────────────────────────────────────────────────────

void NotesWindow::selectFirst_()
{
    const auto& notes = store_.Notes();
    selectedId_ = notes.empty() ? std::string{} : notes.front().id;
}

void NotesWindow::advanceSelectionAfterDelete_(const std::string& deletedId)
{
    const auto& notes = store_.Notes();
    if (notes.empty()) {
        selectedId_.clear();
        return;
    }
    // The to-be-deleted note is still in the list at this point. Pick the
    // successor if there is one, otherwise the predecessor, otherwise (it was
    // the only note) clear the selection.
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].id == deletedId) {
            if (i + 1 < notes.size())      selectedId_ = notes[i + 1].id;  // successor
            else if (i > 0)                selectedId_ = notes[i - 1].id;  // predecessor
            else                           selectedId_.clear();             // sole note
            return;
        }
    }
    // Deleted id not found in list; fall back to first.
    selectFirst_();
}

void NotesWindow::enterEdit_(Trailpack::Note* note)
{
    if (!note) return;
    bodyEditor_.SetText(note->body);
    titleBuffer_ = note->title;
    mode_ = Mode::Edit;
}

void NotesWindow::commitEdit_()
{
    Trailpack::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);
    if (note) {
        note->body  = bodyEditor_.GetText();
        note->title = titleBuffer_;
        Save();
    }
    mode_ = Mode::View;
    bodyEditor_.Clear();
    titleBuffer_.clear();
}

void NotesWindow::cancelEdit_()
{
    mode_ = Mode::View;
    bodyEditor_.Clear();
    titleBuffer_.clear();
}

void NotesWindow::resolvePending_()
{
    if (pendingCreate_) {
        pendingCreate_ = false;
        pendingSelectId_.clear();
        // Create the new note, select it, and open in Edit mode.
        Trailpack::Note& n = store_.Create(Tr("Untitled note"), pendingCreateGroupId_);
        pendingCreateGroupId_.clear();
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    } else if (!pendingSelectId_.empty()) {
        selectedId_ = pendingSelectId_;
        pendingSelectId_.clear();
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
        if (pendingEditId_ == selectedId_) enterEdit_(store_.Get(selectedId_));
    }
    pendingEditId_.clear();
}

bool NotesWindow::editIsDirty_()
{
    if (mode_ != Mode::Edit) return false;
    const Trailpack::Note* cur = store_.Get(selectedId_);
    return cur != nullptr &&
           (bodyEditor_.GetText() != cur->body || titleBuffer_ != cur->title);
}

void NotesWindow::requestSelect_(const std::string& id)
{
    if (id == selectedId_) return;  // no-op

    if (editIsDirty_()) {
        pendingSelectId_ = id;
        pendingCreate_   = false;
        requestPopup_(Popup::Unsaved);
    } else {
        selectedId_ = id;
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestCreate_(const std::string& groupId)
{
    if (editIsDirty_()) {
        pendingCreate_        = true;
        pendingCreateGroupId_ = groupId;
        pendingSelectId_.clear();
        requestPopup_(Popup::Unsaved);
    } else {
        Trailpack::Note& n = store_.Create(Tr("Untitled note"), groupId);
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    }
}

// ── Render ────────────────────────────────────────────────────────────────

void NotesWindow::Render()
{
    // Modals first, so a popup requested last frame opens with its content this frame.
    pollFileDialog_();
    renderModals_();

    // Daily/Weekly resets: uncheck lists whose reset passed (the note open in the editor
    // waits until the edit ends, so Save can't undo the reset).
    if (store_.ApplyResets((int64_t)std::time(nullptr),
                           mode_ == Mode::Edit ? selectedId_ : std::string()) > 0)
        Save();

    // Left column, splitter, right pane -- side by side at full height.
    const float listWidth = renderListColumn_();
    renderSplitter_(listWidth);
    renderDetailPane_();

    // Clicks and menu picks from inside child windows are carried out here, in the
    // window's own scope, where the modals they may open live.
    handleRequests_();
    openRequestedPopups_();
}

void NotesWindow::handleRequests_()
{
    if (createRequested_) {
        createRequested_ = false;
        requestCreate_(createRequestGroupId_);
        createRequestGroupId_.clear();
    }
    if (!selectRequestId_.empty()) {
        requestSelect_(selectRequestId_);
        selectRequestId_.clear();
    }

    // "Edit" from a context menu: select the note (may ask about unsaved changes first),
    // then open it in the editor.
    if (!editRequestId_.empty()) {
        const std::string id = editRequestId_;
        editRequestId_.clear();
        if (store_.Get(id) != nullptr && !(mode_ == Mode::Edit && selectedId_ == id)) {
            pendingEditId_ = id;
            if (id != selectedId_) requestSelect_(id);
            if (selectedId_ == id && mode_ == Mode::View) {   // selected without a modal
                enterEdit_(store_.Get(id));
                pendingEditId_.clear();
            }
        }
    }

    if (!deleteRequestId_.empty()) {
        pendingDeleteId_ = deleteRequestId_;
        deleteRequestId_.clear();
        requestPopup_(Popup::ConfirmDelete);
    }
}

void NotesWindow::performDelete_(const std::string& id)
{
    if (id.empty()) return;
    // Only deleting the OPEN note touches the editor / selection; a note deleted from
    // the list's context menu may be another one.
    if (id == selectedId_) {
        if (mode_ == Mode::Edit) cancelEdit_();  // drop any edit state first
        advanceSelectionAfterDelete_(id);        // pick a neighbour (note still present)
    }
    store_.Delete(id);
    Save();
}

// ── Per-frame caches ────────────────────────────────────────────────────────

const std::vector<Trailpack::Md::Line>& NotesWindow::parsed_(ParseCache& cache, const std::string& body)
{
    if (!cache.valid || cache.body != body) {
        cache.body  = body;
        cache.lines = Trailpack::Md::Parse(body);
        cache.valid = true;
    }
    return cache.lines;
}

Trailpack::Md::TaskCount NotesWindow::taskCount_(const Trailpack::Note& note)
{
    // Drop entries of deleted notes now and then (ids are never reused).
    if (taskCache_.size() > store_.Notes().size() * 2 + 16) taskCache_.clear();

    const size_t hash = std::hash<std::string>{}(note.body);
    auto it = taskCache_.find(note.id);
    if (it == taskCache_.end() || it->second.bodyHash != hash) {
        TaskCache entry;
        entry.bodyHash = hash;
        entry.count    = Trailpack::Md::CountTasks(note.body);
        it = taskCache_.insert_or_assign(note.id, entry).first;
    }
    return it->second.count;
}

// ── Fonts ─────────────────────────────────────────────────────────────────

ImFont* NotesWindow::fontBig_() const
{
    if (api_ == nullptr) return nullptr;
    auto* nl = static_cast<NexusLinkData_t*>(api_->DataLink_Get(DL_NEXUS_LINK));
    return nl != nullptr ? static_cast<ImFont*>(nl->FontBig) : nullptr;
}
