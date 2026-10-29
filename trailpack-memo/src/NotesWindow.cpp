// NotesWindow.cpp — Two-pane notes UI + JSON persistence for Trailpack Memo.

#include "NotesWindow.h"

#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    }

    if (!notesFile_.empty() && fs::exists(notesFile_)) {
        std::ifstream f(notesFile_);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            store_.FromJson(ss.str());  // rolls back silently on bad JSON
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
    const float createBtnH = ImGui::GetFrameHeightWithSpacing();

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
                                   ImGui::GetColorU32(Theme::BORDER_GOLD),
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
                                   ImGui::GetColorU32(Theme::BORDER_GOLD),
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
