// NotesWindow.cpp — Two-pane notes UI + JSON persistence for Trailpack Memo.

#include "NotesWindow.h"

#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <commdlg.h>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "Theme.h"
#include "Markdown.h"
#include "MarkdownRender.h"
#include "MarkdownEdit.h"

namespace fs = std::filesystem;

// ── Init ──────────────────────────────────────────────────────────────────

void NotesWindow::Init(AddonAPI_t* api)
{
    api_ = api;

    const char* dir = api->Paths_GetAddonDirectory("TrailpackMemo");
    if (dir && dir[0] != '\0') {
        fs::path addonDir(dir);
        std::error_code ec;
        fs::create_directories(addonDir, ec);   // no-op if already exists
        notesFile_ = addonDir / "notes.json";
        exportDir_ = addonDir / "exports";
    }

    if (!notesFile_.empty() && fs::exists(notesFile_)) {
        std::ifstream f(notesFile_);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            // Rolls back silently on bad JSON. On success, save at once: loading may have
            // replaced legacy ids, and exported ids must stay stable across sessions.
            if (store_.FromJson(ss.str())) Save();
        }
    }

    selectFirst_();
}

// ── Save / Shutdown ───────────────────────────────────────────────────────

void NotesWindow::Save()
{
    if (notesFile_.empty()) return;

    std::ofstream f(notesFile_, std::ios::out | std::ios::trunc);
    if (f.is_open())
        f << store_.ToJson();
}

void NotesWindow::Shutdown()
{
    // A file dialog still open on its worker thread must finish before the DLL goes
    // away; this blocks the unload until the user closes it (the dialog stays visible).
    if (ioDialog_.valid()) ioDialog_.wait();
    Save();
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

void NotesWindow::enterEdit_(Magpie::Note* note)
{
    if (!note) return;
    bodyEditor_.SetText(note->body);
    titleBuffer_ = note->title;
    mode_ = Mode::Edit;
}

void NotesWindow::commitEdit_()
{
    Magpie::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);
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
        Magpie::Note& n = store_.Create("Untitled note");
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    } else if (!pendingSelectId_.empty()) {
        selectedId_ = pendingSelectId_;
        pendingSelectId_.clear();
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestSelect_(const std::string& id)
{
    if (id == selectedId_) return;  // no-op

    bool dirty = (mode_ == Mode::Edit);
    if (dirty) {
        Magpie::Note* cur = store_.Get(selectedId_);
        if (cur) {
            dirty = (bodyEditor_.GetText() != cur->body) || (titleBuffer_ != cur->title);
        }
    }

    if (dirty) {
        pendingSelectId_ = id;
        pendingCreate_   = false;
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        selectedId_ = id;
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestCreate_()
{
    bool dirty = (mode_ == Mode::Edit);
    if (dirty) {
        Magpie::Note* cur = store_.Get(selectedId_);
        if (cur) {
            dirty = (bodyEditor_.GetText() != cur->body) || (titleBuffer_ != cur->title);
        }
    }

    if (dirty) {
        pendingCreate_   = true;
        pendingSelectId_.clear();
        ImGui::OpenPopup("Unsaved Changes");
    } else {
        Magpie::Note& n = store_.Create("Untitled note");
        selectedId_ = n.id;
        Save();
        enterEdit_(&n);
    }
}

// ── Unsaved-changes modal ─────────────────────────────────────────────────

void NotesWindow::renderUnsavedModal_()
{
    // Centre the popup on screen.
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 centre  = ImVec2(display.x * 0.5f, display.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This note has unsaved changes.");
        ImGui::Spacing();

        Theme::PushGreenButton();
        if (ImGui::Button("Save", ImVec2(80, 0))) {
            commitEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Discard", ImVec2(80, 0))) {
            cancelEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            pendingSelectId_.clear();
            pendingCreate_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ── Render ────────────────────────────────────────────────────────────────

void NotesWindow::Render()
{
    // Draw the modals first so they can open this frame if triggered below.
    renderUnsavedModal_();
    renderDeleteModal_();
    pollFileDialog_();
    renderIoModal_();
    renderImportModal_();

    const auto& notes = store_.Notes();

    // A Delete button click (inside the detail child below) is captured here and
    // resolved AFTER the child ends, so OpenPopup runs in the parent-window scope
    // where renderDeleteModal_'s BeginPopupModal lives.
    std::string deleteClickedId;

    // ── Left column: the note list with the "Create New Note" button directly
    //    beneath it. Grouped so the right-hand panel (added with SameLine below)
    //    sits beside the FULL height of this column — not squished under the
    //    button at the bottom of the window. ─────────────────────────────────
    const float leftWidth  = 200.0f;
    const float createBtnH = ImGui::GetFrameHeightWithSpacing() * 2.0f;   // two button rows

    ImGui::BeginGroup();

    // The list fills the column's height minus the Create button row below it
    // (negative height = fill available minus that many px).
    Theme::PushDarkFrameBg();
    ImGui::BeginChild("##notes_list", ImVec2(leftWidth, -createBtnH), true);
    Theme::PopFrameBg();

    // Capture a click here but defer the navigation until after the group:
    // OpenPopup() (inside requestSelect_) must run in the parent-window scope so
    // the unsaved-changes modal opens — never from inside the list child.
    std::string clickedId;
    for (const auto& note : notes) {
        bool selected = (note.id == selectedId_);
        // Title drawn manually (clipped) so a task-progress count can sit right-aligned
        // without long titles running over it. PushID keeps duplicate titles distinct.
        ImGui::PushID(note.id.c_str());
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##note_row", selected)) {
            clickedId = note.id;
        }
        if (ImGui::BeginPopupContextItem("##row_ctx")) {
            if (ImGui::MenuItem("Export note...")) startExport_(note.id);
            ImGui::EndPopup();
        }
        ImGui::PopID();

        const float rowRight = ImGui::GetItemRectMax().x;
        const float rowH     = ImGui::GetItemRectSize().y;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        char progress[24] = "";
        float progressW = 0.0f;
        const Magpie::Md::TaskCount tc = Magpie::Md::CountTasks(note.body);
        if (tc.total > 0) {
            snprintf(progress, sizeof progress, "%d/%d", tc.done, tc.total);
            progressW = ImGui::CalcTextSize(progress).x;
            const ImU32 col = (tc.done == tc.total)
                ? ImGui::GetColorU32(Theme::BTN_GREEN_ACTIVE)
                : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            dl->AddText(ImVec2(rowRight - progressW, rowPos.y), col, progress);
        }

        const float titleRight = rowRight - (progressW > 0.0f ? progressW + 6.0f : 0.0f);
        dl->PushClipRect(rowPos, ImVec2(titleRight, rowPos.y + rowH), true);
        dl->AddText(rowPos, ImGui::GetColorU32(ImGuiCol_Text), note.title.c_str());
        dl->PopClipRect();
    }

    ImGui::EndChild();

    // Create New Note button — immediately below the list, inside the column.
    Theme::PushGreenButton();
    if (ImGui::Button("Create New Note", ImVec2(leftWidth, 0))) {
        requestCreate_();
    }
    Theme::PopButton();

    // Import / Export all — second row, split in half.
    {
        const float half = (leftWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Import...", ImVec2(half, 0))) startImport_();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add notes from an exported file (or a notes.json).\n"
                              "Existing notes are never replaced.");
        ImGui::SameLine();
        if (ImGui::Button("Export all...", ImVec2(half, 0))) startExport_(std::string());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save all notes to one file.\n"
                              "Right-click a note in the list to export just that one.");
    }

    ImGui::EndGroup();

    if (!clickedId.empty()) {
        requestSelect_(clickedId);
    }

    // ── Right panel: the note view/editor, beside the list at full height. ──
    ImGui::SameLine();

    ImGui::BeginChild("##notes_detail", ImVec2(0, 0), true);

    Magpie::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);

    if (note == nullptr) {
        // Empty state — light magpie flavour, all ASCII.
        ImGui::Spacing();
        ImGui::TextDisabled("Your nest is empty.");
        ImGui::TextDisabled("Tap 'Create New Note' to start collecting.");
    } else if (mode_ == Mode::Edit) {
        // ── Edit mode ──────────────────────────────────────────────────────

        // Save / Cancel buttons at the top.
        Theme::PushGreenButton();
        if (ImGui::Button("Save", ImVec2(70, 0))) {
            commitEdit_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) {
            cancelEdit_();
        }
        Theme::PopButton();

        // Delete button on the far right, same row.
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - 60.0f);
        }
        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(60.0f, 0))) {
            deleteClickedId = note->id;   // confirm before deleting
        }
        Theme::PopButton();

        ImGui::Separator();

        if (note != nullptr) {
            // Editable title field.
            Theme::PushEditBg();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##title", &titleBuffer_);
            Theme::PopFrameBg();

            ImGui::Spacing();

            renderEditToolbar_();

            // Editable body — chat codes render as atomic chip cells;
            // markdown syntax stays as raw text (edit = source view).
            if (showPreview_) {
                // Source on the left, live rendered preview on the right.
                const float half = (ImGui::GetContentRegionAvail().x -
                                    ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                ImGui::BeginChild("##edit_col", ImVec2(half, 0), false);
                bodyEditor_.Render("##body_edit",
                                   ImGui::GetContentRegionAvail().x,
                                   ImGui::GetColorU32(Theme::Colors().border),
                                   /*minLines=*/6);
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##preview_col", ImVec2(0, 0), true);
                const std::vector<Magpie::Md::Line> lines =
                    Magpie::Md::Parse(bodyEditor_.GetText());
                Magpie::RenderMarkdown(lines, fontBig_(), ImGui::GetContentRegionAvail().x);
                ImGui::EndChild();
            } else {
                bodyEditor_.Render("##body_edit",
                                   ImGui::GetContentRegionAvail().x,
                                   ImGui::GetColorU32(Theme::Colors().border),
                                   /*minLines=*/6);
            }
        }
    } else {
        // ── View mode ──────────────────────────────────────────────────────

        // Title row with Edit and Delete buttons on the right.
        ImGui::Text("%s", note->title.c_str());

        // Edit button right-aligned before Delete.
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - 60.0f - 65.0f - ImGui::GetStyle().ItemSpacing.x);
        }
        Theme::PushGreenButton();
        if (ImGui::Button("Edit", ImVec2(60.0f, 0))) {
            enterEdit_(note);
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(60.0f, 0))) {
            deleteClickedId = note->id;   // confirm before deleting
        }
        Theme::PopButton();

        ImGui::Separator();

        if (note != nullptr) {
            renderViewBody_(note);
        }
    }

    ImGui::EndChild();

    // Open the delete-confirmation modal in the parent-window scope (matches the
    // BeginPopupModal in renderDeleteModal_).
    if (!deleteClickedId.empty()) {
        pendingDeleteId_ = deleteClickedId;
        ImGui::OpenPopup("Confirm Delete");
    }
}

// ── Markdown helpers ────────────────────────────────────────────────────────

ImFont* NotesWindow::fontBig_() const
{
    if (api_ == nullptr) return nullptr;
    auto* nl = static_cast<NexusLinkData_t*>(api_->DataLink_Get(DL_NEXUS_LINK));
    return nl != nullptr ? static_cast<ImFont*>(nl->FontBig) : nullptr;
}

void NotesWindow::renderEditToolbar_()
{
    using namespace Magpie::Md;

    // Labels stay ASCII: Nexus's font has no symbol glyphs.
    auto toolButton = [](const char* label, const char* tip) {
        const bool pressed = ImGui::Button(label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return pressed;
    };

    // Selection is read before the buttons: clicking one blurs the editor but
    // stb keeps the selection, and ApplyEdit refocuses it.
    int selA = 0, selB = 0;
    bodyEditor_.GetSelection(selA, selB);

    EditResult r;
    if (toolButton(" B ",     "Bold (Ctrl+B)"))
        r = WrapSelection(bodyEditor_.Codepoints(), selA, selB, "**");
    if (toolButton(" I ",     "Italic (Ctrl+I)"))
        r = WrapSelection(bodyEditor_.Codepoints(), selA, selB, "*");
    if (toolButton("H1",      "Heading 1"))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Heading1);
    if (toolButton("H2",      "Heading 2"))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Heading2);
    if (toolButton("- List",  "Bullet list"))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Bullet);
    if (toolButton("[ ] Task", "Checkbox (task list)"))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Task);
    bodyEditor_.ApplyEdit(r);

    // Preview toggle, right-aligned on the same row.
    const char* previewLabel = "Preview";
    const float boxW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                       ImGui::CalcTextSize(previewLabel).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > boxW) ImGui::SameLine(ImGui::GetCursorPosX() + avail - boxW);
    else              ImGui::NewLine();
    if (ImGui::Checkbox(previewLabel, &showPreview_)) settingsChanged_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show a live preview beside the editor");
}

void NotesWindow::renderViewBody_(Magpie::Note* note)
{
    // Rendered markdown inside a scrollable child so long notes scroll.
    // EDIT mode shows the raw source; VIEW shows this.
    ImGui::BeginChild("##view_body", ImVec2(0, 0), false);

    const std::vector<Magpie::Md::Line> lines = Magpie::Md::Parse(note->body);
    const int clickedTask = Magpie::RenderMarkdown(lines, fontBig_(),
                                                   ImGui::GetContentRegionAvail().x,
                                                   /*interactiveTasks=*/true);
    if (clickedTask >= 0 && Magpie::Md::ToggleTask(note->body, clickedTask)) {
        Save();
    }

    // Right-click on the body (but not on a chip, which opens its own menu
    // during RenderMarkdown) -> note context menu.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::OpenPopup("##view_ctx");
    }
    if (ImGui::BeginPopup("##view_ctx")) {
        const Magpie::Md::TaskCount tc = Magpie::Md::CountTasks(note->body);
        if (ImGui::MenuItem("Uncheck all tasks", nullptr, false, tc.done > 0)) {
            Magpie::Md::UncheckAllTasks(note->body);
            Save();
        }
        if (ImGui::MenuItem("Export note...")) startExport_(note->id);
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

// ── Delete confirmation ─────────────────────────────────────────────────────

void NotesWindow::performDelete_(const std::string& id)
{
    if (id.empty()) return;
    if (mode_ == Mode::Edit) cancelEdit_();      // drop any edit state first
    advanceSelectionAfterDelete_(id);            // pick a neighbour (note still present)
    store_.Delete(id);
    Save();
}

void NotesWindow::renderDeleteModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Confirm Delete", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete this note? This cannot be undone.");
        ImGui::Spacing();

        Theme::PushAmberButton();
        if (ImGui::Button("Delete", ImVec2(80, 0))) {
            performDelete_(pendingDeleteId_);
            pendingDeleteId_.clear();
            ImGui::CloseCurrentPopup();
        }
        Theme::PopButton();

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            pendingDeleteId_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ── Export / import ─────────────────────────────────────────────────────────

namespace {

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

// A note title as a safe file name: ASCII letters/digits/-/_ kept, spaces -> '_'.
std::string FileNameFor(const std::string& title)
{
    std::string out;
    for (unsigned char c : title) {
        if (std::isalnum(c) || c == '-' || c == '_') out += (char)c;
        else if (c == ' ' && !out.empty() && out.back() != '_') out += '_';
        if (out.size() >= 60) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "note" : out;
}

// Modal Windows open/save dialog (runs on a worker thread). Returns the chosen
// path, or empty if cancelled.
std::wstring RunFileDialog(bool save, std::wstring dir, std::wstring fileName)
{
    wchar_t buf[MAX_PATH * 4] = L"";
    if (save) fileName.copy(buf, (sizeof buf / sizeof buf[0]) - 1);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize     = sizeof ofn;
    ofn.lpstrFilter     = L"Trailpack Memo notes (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = sizeof buf / sizeof buf[0];
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt     = L"json";
    ofn.lpstrTitle      = save ? L"Export notes" : L"Import notes";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(buf) : std::wstring();
}

} // namespace

void NotesWindow::startExport_(const std::string& id)
{
    if (ioDialog_.valid()) return;   // one dialog at a time

    std::string name;
    if (id.empty()) {
        char date[16] = "";
        const std::time_t now = std::time(nullptr);
        std::strftime(date, sizeof date, "%Y-%m-%d", std::localtime(&now));
        name = std::string("trailpack-notes-") + date + ".json";
    } else {
        const Magpie::Note* note = store_.Get(id);
        if (note == nullptr) return;
        name = FileNameFor(note->title) + ".json";
    }

    std::error_code ec;
    if (!exportDir_.empty()) fs::create_directories(exportDir_, ec);

    ioAction_   = IoAction::Export;
    ioExportId_ = id;
    ioDialog_   = std::async(std::launch::async, RunFileDialog, true,
                             exportDir_.wstring(), Widen(name));
}

void NotesWindow::startImport_()
{
    if (ioDialog_.valid()) return;
    std::error_code ec;
    if (!exportDir_.empty()) fs::create_directories(exportDir_, ec);

    ioAction_ = IoAction::Import;
    ioDialog_ = std::async(std::launch::async, RunFileDialog, false,
                           exportDir_.wstring(), std::wstring());
}

void NotesWindow::pollFileDialog_()
{
    if (!ioDialog_.valid() ||
        ioDialog_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    const fs::path path(ioDialog_.get());
    const IoAction action = ioAction_;
    ioAction_ = IoAction::None;
    if (path.empty()) return;   // cancelled

    const std::string fileName = path.filename().u8string();
    char msg[512];

    if (action == IoAction::Export) {
        std::vector<std::string> ids;
        if (!ioExportId_.empty()) ids.push_back(ioExportId_);
        const size_t count = ids.empty() ? store_.Notes().size() : 1;

        std::ofstream f(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (f.is_open()) f << store_.ExportJson(ids);
        if (f.is_open() && f.good())
            snprintf(msg, sizeof msg, "Exported %zu note%s to %s.", count,
                     count == 1 ? "" : "s", fileName.c_str());
        else
            snprintf(msg, sizeof msg, "Could not write %s.", fileName.c_str());
    } else {
        std::ifstream f(path, std::ios::in | std::ios::binary);
        std::ostringstream ss;
        if (f.is_open()) ss << f.rdbuf();

        std::vector<Magpie::ImportItem> items;
        const bool valid = f.is_open() && store_.PrepareImport(ss.str(), items);
        const size_t offered = std::count_if(items.begin(), items.end(),
            [](const Magpie::ImportItem& i) { return !i.duplicate || i.changed; });
        if (!valid) {
            snprintf(msg, sizeof msg,
                     "%s is not a Trailpack Memo export or notes file.\nNothing was imported.",
                     fileName.c_str());
        } else if (items.empty()) {
            snprintf(msg, sizeof msg, "%s contains no notes.", fileName.c_str());
        } else if (offered == 0) {
            snprintf(msg, sizeof msg,
                     "All notes in %s already exist and are up to date.\nNothing was imported.",
                     fileName.c_str());
        } else {
            // Let the user pick: new notes pre-selected; updates of existing notes are
            // offered but never pre-selected; unchanged duplicates are shown locked.
            importItems_ = std::move(items);
            importChecked_.assign(importItems_.size(), 0);
            for (size_t i = 0; i < importItems_.size(); ++i)
                importChecked_[i] = importItems_[i].duplicate ? 0 : 1;
            importFile_ = fileName;
            ImGui::OpenPopup("Import Notes");
            return;
        }
    }

    ioMessage_ = msg;
    ImGui::OpenPopup("Import / Export");
}

void NotesWindow::renderIoModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Import / Export", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(ioMessage_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            ioMessage_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void NotesWindow::renderImportModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Import Notes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Notes in %s:", importFile_.c_str());
    ImGui::Spacing();

    // An update is only possible when the local note is not open in the editor
    // (saving the edit afterwards would silently undo the update).
    auto updatable = [&](const Magpie::ImportItem& item) {
        return item.changed && !(mode_ == Mode::Edit && selectedId_ == item.id);
    };
    auto selectable = [&](const Magpie::ImportItem& item) {
        return !item.duplicate || updatable(item);
    };

    // Scrollable list once the file holds more than a handful of notes.
    const float rowH  = ImGui::GetFrameHeightWithSpacing();
    const float listH = rowH * (float)std::min<size_t>(importItems_.size(), 10);
    ImGui::BeginChild("##import_list", ImVec2(400.0f, listH), false);
    int toAdd = 0, toUpdate = 0;
    for (size_t i = 0; i < importItems_.size(); ++i) {
        const Magpie::ImportItem& item = importItems_[i];
        ImGui::PushID((int)i);
        const char* title = item.title.empty() ? "(untitled)" : item.title.c_str();
        if (selectable(item)) {
            bool on = importChecked_[i] != 0;
            if (item.duplicate) {
                // Exists locally with different title/text: ask before replacing it.
                char label[300];
                snprintf(label, sizeof label, "Update: %s", title);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
                if (ImGui::Checkbox(label, &on)) importChecked_[i] = on ? 1 : 0;
                ImGui::PopStyleColor(1);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("You already have this note, but the file's version differs.\n"
                                      "Tick to replace your title and text with the file's version.");
                if (on) ++toUpdate;
            } else {
                if (ImGui::Checkbox(title, &on)) importChecked_[i] = on ? 1 : 0;
                if (on) ++toAdd;
            }
        } else if (item.changed) {
            ImGui::TextDisabled("[-] %s  (open in the editor - save or cancel first)", title);
        } else {
            // Already in the notes (same id, or same title + text) and identical.
            ImGui::TextDisabled("[-] %s  (already up to date)", title);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::SmallButton("All")) {
        for (size_t i = 0; i < importItems_.size(); ++i)
            importChecked_[i] = selectable(importItems_[i]) ? 1 : 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        std::fill(importChecked_.begin(), importChecked_.end(), 0);
    }
    ImGui::Separator();

    char label[64];
    if (toAdd > 0 && toUpdate > 0)
        snprintf(label, sizeof label, "Import %d, update %d", toAdd, toUpdate);
    else if (toUpdate > 0)
        snprintf(label, sizeof label, "Update %d note%s", toUpdate, toUpdate == 1 ? "" : "s");
    else
        snprintf(label, sizeof label, "Import %d note%s", toAdd, toAdd == 1 ? "" : "s");
    Theme::PushGreenButton();
    const bool doImport = ImGui::Button(label, ImVec2(160, 0)) && (toAdd + toUpdate) > 0;
    Theme::PopButton();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(80, 0));

    if (doImport) {
        std::vector<Magpie::ImportItem> adds, updates;
        for (size_t i = 0; i < importItems_.size(); ++i) {
            const Magpie::ImportItem& item = importItems_[i];
            if (!importChecked_[i] || !selectable(item)) continue;
            (item.duplicate ? updates : adds).push_back(item);
        }

        const size_t before = store_.Notes().size();
        const int added   = store_.AddImported(adds);
        const int updated = store_.UpdateImported(updates);
        if (added + updated > 0) {
            Save();
            // Show the first imported (else updated) note, unless an edit is in progress.
            if (mode_ == Mode::View)
                selectedId_ = added > 0 ? store_.Notes()[before].id : updates.front().id;
        }
    }
    if (doImport || cancel) {
        importItems_.clear();
        importChecked_.clear();
        importFile_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

