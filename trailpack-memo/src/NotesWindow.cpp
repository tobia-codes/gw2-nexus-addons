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
#include "I18n.h"
#include "FontScale.h"

namespace fs = std::filesystem;
using Magpie::I18n::Tr;
using Magpie::I18n::TrN;

// "<translated title>###<stable id>": a popup keeps its ImGui id when the language
// changes, so OpenPopup and BeginPopupModal always agree.
static std::string PopupName(const char* en, const char* id)
{
    return std::string(Tr(en)) + "###" + id;
}

// Button width that fits its (translated) label, but never narrower than `minW`.
static float ButtonWidth(const char* label, float minW)
{
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    return w > minW ? w : minW;
}

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
            const std::string content = ss.str();
            // Rolls back silently on bad JSON. On success, save at once: loading may have
            // replaced legacy ids, and exported ids must stay stable across sessions.
            if (store_.FromJson(content)) {
                // First load of a version-1 file (plain array): keep a copy before it is
                // rewritten in the version-2 format, which older builds can't read.
                const fs::path backup = notesFile_.parent_path() / "notes.v1-backup.json";
                const size_t first = content.find_first_not_of(" \t\r\n");
                std::error_code ec;
                if (first != std::string::npos && content[first] == '[' && !fs::exists(backup, ec)) {
                    std::ofstream b(backup, std::ios::out | std::ios::trunc | std::ios::binary);
                    if (b.is_open()) b << content;
                }
                Save();
            }
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
        Magpie::Note& n = store_.Create(Tr("Untitled note"), pendingCreateGroupId_);
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
        ImGui::OpenPopup(PopupName("Unsaved Changes", "unsaved").c_str());
    } else {
        selectedId_ = id;
        mode_ = Mode::View;
        bodyEditor_.Clear();
        titleBuffer_.clear();
    }
}

void NotesWindow::requestCreate_(const std::string& groupId)
{
    bool dirty = (mode_ == Mode::Edit);
    if (dirty) {
        Magpie::Note* cur = store_.Get(selectedId_);
        if (cur) {
            dirty = (bodyEditor_.GetText() != cur->body) || (titleBuffer_ != cur->title);
        }
    }

    if (dirty) {
        pendingCreate_        = true;
        pendingCreateGroupId_ = groupId;
        pendingSelectId_.clear();
        ImGui::OpenPopup(PopupName("Unsaved Changes", "unsaved").c_str());
    } else {
        Magpie::Note& n = store_.Create(Tr("Untitled note"), groupId);
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

    if (ImGui::BeginPopupModal(PopupName("Unsaved Changes", "unsaved").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Tr("This note has unsaved changes."));
        ImGui::Spacing();

        Theme::PushGreenButton();
        if (ImGui::Button(Tr("Save"), ImVec2(ButtonWidth(Tr("Save"), 80), 0))) {
            commitEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button(Tr("Discard"), ImVec2(ButtonWidth(Tr("Discard"), 80), 0))) {
            cancelEdit_();
            ImGui::CloseCurrentPopup();
            resolvePending_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        if (ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0))) {
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
    renderGroupNameModal_();
    renderDeleteGroupModal_();
    renderBulkDeleteModal_();
    renderGroupColorModal_();

    const auto& notes = store_.Notes();

    // A Delete button click (inside the detail child below) is captured here and
    // resolved AFTER the child ends, so OpenPopup runs in the parent-window scope
    // where renderDeleteModal_'s BeginPopupModal lives.
    std::string deleteClickedId;

    // ── Left column: the note list with the "Create New Note" button directly
    //    beneath it. Grouped so the right-hand panel (added with SameLine below)
    //    sits beside the FULL height of this column — not squished under the
    //    button at the bottom of the window. ─────────────────────────────────
    // The list width is user-resizable (splitter below); keep room for the detail pane.
    const float minList   = 120.0f;
    const float maxList   = ImGui::GetContentRegionAvail().x - 200.0f;
    // Clamped for drawing only: the saved width survives a temporarily narrow window.
    const float leftWidth  = maxList > minList
        ? (listWidth_ < minList ? minList : listWidth_ > maxList ? maxList : listWidth_)
        : minList;
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
    std::string createInGroup;
    bool openGroupName = false, openDeleteGroup = false, openBulkDelete = false;
    bool openGroupColor = false;
    const float groupIndent = ImGui::GetFontSize() * 0.9f;

    // Groups first (each followed by its notes unless collapsed), then ungrouped notes.
    // Ids are snapshotted: a header's context menu may change the store this frame.
    std::vector<std::string> groupIds;
    for (const auto& g : store_.Groups()) groupIds.push_back(g.id);
    for (const auto& gid : groupIds) {
        Magpie::Group* group = store_.GetGroup(gid);
        if (group == nullptr) continue;
        if (!renderGroupHeader_(*group, openGroupName, openDeleteGroup, openGroupColor, createInGroup))
            continue;
        for (const auto& note : notes)
            if (note.groupId == gid) renderNoteRow_(note, groupIndent, clickedId);
    }

    const bool anyUngrouped = std::any_of(notes.begin(), notes.end(),
        [](const Magpie::Note& n) { return n.groupId.empty(); });
    if (!groupIds.empty() && (anyUngrouped || ImGui::GetDragDropPayload() != nullptr)) {
        // Section label; also the drop target that takes a note out of its group.
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("No group"));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("TPM_NOTE")) {
                const std::string id(static_cast<const char*>(p->Data), (size_t)p->DataSize);
                if (store_.MoveToGroup(id, std::string())) Save();
            }
            ImGui::EndDragDropTarget();
        }
    }
    for (const auto& note : notes)
        if (note.groupId.empty()) renderNoteRow_(note, 0.0f, clickedId);

    ImGui::EndChild();

    // Bottom row: "Create New Note" + a square menu button (import / export).
    const float menuW = ImGui::GetFrameHeight();
    const float createW = leftWidth - menuW - ImGui::GetStyle().ItemSpacing.x;
    Theme::PushGreenButton();
    if (ImGui::Button(Tr("Create New Note"), ImVec2(createW, 0))) {
        requestCreate_();
    }
    Theme::PopButton();

    ImGui::SameLine();
    if (ImGui::Button("##notes_menu_btn", ImVec2(menuW, menuW))) {
        ImGui::OpenPopup("##notes_menu");
    }
    {
        // Hamburger icon drawn with lines: Nexus's font has no symbol glyphs, and it
        // reads the same in every language.
        const ImVec2 bMin = ImGui::GetItemRectMin();
        const ImVec2 bMax = ImGui::GetItemRectMax();
        const float  padX = menuW * 0.25f;
        const float  gap  = menuW * 0.18f;
        const float  midY = (bMin.y + bMax.y) * 0.5f;
        const ImU32  col  = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        for (int i = -1; i <= 1; ++i) {
            const float y = midY + i * gap;
            dl->AddLine(ImVec2(bMin.x + padX, y), ImVec2(bMax.x - padX, y), col, 1.5f);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Menu"));

    if (ImGui::BeginPopup("##notes_menu")) {
        if (ImGui::MenuItem(Tr("New group..."))) {
            groupNameId_.clear();
            groupNameBuffer_ = Tr("New group");
            openGroupName    = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Import..."))) startImport_();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Add notes from an exported file (or a notes.json).\n"
                                       "Existing notes are only updated if you choose to."));
        if (ImGui::MenuItem(Tr("Export all..."))) startExport_(std::string());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Save all notes to one file.\n"
                                       "Right-click a note in the list to export just that one."));
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Delete notes..."), nullptr, false, !notes.empty())) {
            bulkSelected_.clear();
            bulkConfirm_   = false;
            openBulkDelete = true;
        }
        ImGui::EndPopup();
    }

    ImGui::EndGroup();

    // Popups requested from inside the list child / menus open here, in window scope.
    if (openGroupName)   ImGui::OpenPopup(PopupName("Group", "group_name").c_str());
    if (openDeleteGroup) ImGui::OpenPopup(PopupName("Delete Group", "delete_group").c_str());
    if (openBulkDelete)  ImGui::OpenPopup(PopupName("Delete Notes", "bulk_delete").c_str());
    if (openGroupColor)  ImGui::OpenPopup(PopupName("Group Colour", "group_color").c_str());
    if (!createInGroup.empty()) requestCreate_(createInGroup);

    if (!clickedId.empty()) {
        requestSelect_(clickedId);
    }

    // ── Splitter: drag to resize the list, double-click to reset. ───────────
    {
        const float splitW = 6.0f;
        const float gap    = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(0.0f, (gap - splitW) * 0.5f > 0.0f ? (gap - splitW) * 0.5f : 0.0f);
        ImGui::InvisibleButton("##list_splitter", ImVec2(splitW, ImGui::GetContentRegionAvail().y));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active) listWidth_ = leftWidth + ImGui::GetIO().MouseDelta.x;
        if (ImGui::IsItemDeactivated()) settingsChanged_ = true;   // save once, on release
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            listWidth_       = kDefaultListWidth;
            settingsChanged_ = true;
        }
        if (hovered || active) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const float  x = (a.x + b.x) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y), ImVec2(x, b.y),
                                                ImGui::GetColorU32(Theme::Colors().border), 2.0f);
        }
    }

    // ── Right panel: the note view/editor, beside the list at full height. ──
    ImGui::SameLine(0.0f, (ImGui::GetStyle().ItemSpacing.x - 6.0f) * 0.5f > 0.0f
                              ? (ImGui::GetStyle().ItemSpacing.x - 6.0f) * 0.5f : 0.0f);

    ImGui::BeginChild("##notes_detail", ImVec2(0, 0), true);

    Magpie::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);

    if (note == nullptr) {
        // Empty state — light magpie flavour, all ASCII.
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("Your nest is empty."));
        ImGui::TextDisabled("%s", Tr("Click 'Create New Note' to start collecting."));
    } else if (mode_ == Mode::Edit) {
        // ── Edit mode ──────────────────────────────────────────────────────

        // Save / Cancel buttons at the top.
        Theme::PushGreenButton();
        if (ImGui::Button(Tr("Save"), ImVec2(ButtonWidth(Tr("Save"), 70), 0))) {
            commitEdit_();
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 70), 0))) {
            cancelEdit_();
        }
        Theme::PopButton();

        // Delete button on the far right, same row.
        const float deleteW = ButtonWidth(Tr("Delete"), 60.0f);
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - deleteW);
        }
        Theme::PushAmberButton();
        if (ImGui::Button(Tr("Delete"), ImVec2(deleteW, 0))) {
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
                {
                    Magpie::FontScaleScope noteFont(noteTextScale_);
                    bodyEditor_.Render("##body_edit",
                                       ImGui::GetContentRegionAvail().x,
                                       ImGui::GetColorU32(Theme::Colors().border),
                                       /*minLines=*/6);
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##preview_col", ImVec2(0, 0), true);
                const std::vector<Magpie::Md::Line> lines =
                    Magpie::Md::Parse(bodyEditor_.GetText());
                {
                    Magpie::FontScaleScope noteFont(noteTextScale_);
                    Magpie::RenderMarkdown(lines, fontBig_(), ImGui::GetContentRegionAvail().x);
                }
                ImGui::EndChild();
            } else {
                Magpie::FontScaleScope noteFont(noteTextScale_);
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
        const float editW   = ButtonWidth(Tr("Edit"), 60.0f);
        const float deleteW = ButtonWidth(Tr("Delete"), 60.0f);
        {
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - editW - deleteW - ImGui::GetStyle().ItemSpacing.x);
        }
        Theme::PushGreenButton();
        if (ImGui::Button(Tr("Edit"), ImVec2(editW, 0))) {
            enterEdit_(note);
        }
        Theme::PopButton();

        ImGui::SameLine();

        Theme::PushAmberButton();
        if (ImGui::Button(Tr("Delete"), ImVec2(deleteW, 0))) {
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
        ImGui::OpenPopup(PopupName("Confirm Delete", "confirm_delete").c_str());
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
    if (toolButton(" B ",     Tr("Bold (Ctrl+B)")))
        r = WrapSelection(bodyEditor_.Codepoints(), selA, selB, "**");
    if (toolButton(" I ",     Tr("Italic (Ctrl+I)")))
        r = WrapSelection(bodyEditor_.Codepoints(), selA, selB, "*");
    if (toolButton("H1",      Tr("Heading 1")))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Heading1);
    if (toolButton("H2",      Tr("Heading 2")))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Heading2);
    if (toolButton(Tr("- List"),  Tr("Bullet list")))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Bullet);
    if (toolButton(Tr("[ ] Task"), Tr("Checkbox (task list)")))
        r = ToggleLinePrefix(bodyEditor_.Codepoints(), selA, selB, LinePrefix::Task);
    bodyEditor_.ApplyEdit(r);

    // Preview toggle, right-aligned on the same row.
    const char* previewLabel = Tr("Preview");
    const float boxW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                       ImGui::CalcTextSize(previewLabel).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > boxW) ImGui::SameLine(ImGui::GetCursorPosX() + avail - boxW);
    else              ImGui::NewLine();
    if (ImGui::Checkbox(previewLabel, &showPreview_)) settingsChanged_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Show a live preview beside the editor"));
}

void NotesWindow::renderViewBody_(Magpie::Note* note)
{
    // Rendered markdown inside a scrollable child so long notes scroll.
    // EDIT mode shows the raw source; VIEW shows this.
    ImGui::BeginChild("##view_body", ImVec2(0, 0), false);

    const std::vector<Magpie::Md::Line> lines = Magpie::Md::Parse(note->body);
    int clickedTask = -1;
    {
        Magpie::FontScaleScope noteFont(noteTextScale_);
        clickedTask = Magpie::RenderMarkdown(lines, fontBig_(),
                                             ImGui::GetContentRegionAvail().x,
                                             /*interactiveTasks=*/true);
    }
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
        if (ImGui::MenuItem(Tr("Uncheck all tasks"), nullptr, false, tc.done > 0)) {
            Magpie::Md::UncheckAllTasks(note->body);
            Save();
        }
        if (ImGui::MenuItem(Tr("Export note..."))) startExport_(note->id);
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

    if (ImGui::BeginPopupModal(PopupName("Confirm Delete", "confirm_delete").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Tr("Delete this note? This cannot be undone."));
        ImGui::Spacing();

        Theme::PushAmberButton();
        if (ImGui::Button(Tr("Delete"), ImVec2(ButtonWidth(Tr("Delete"), 80), 0))) {
            performDelete_(pendingDeleteId_);
            pendingDeleteId_.clear();
            ImGui::CloseCurrentPopup();
        }
        Theme::PopButton();

        ImGui::SameLine();

        if (ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0))) {
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
std::wstring RunFileDialog(bool save, std::wstring dir, std::wstring fileName,
                           std::wstring title, std::wstring filter)
{
    wchar_t buf[MAX_PATH * 4] = L"";
    if (save) fileName.copy(buf, (sizeof buf / sizeof buf[0]) - 1);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize     = sizeof ofn;
    ofn.lpstrFilter     = filter.c_str();   // "label\0pattern\0...\0" (see DialogFilter)
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = sizeof buf / sizeof buf[0];
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt     = L"json";
    ofn.lpstrTitle      = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(buf) : std::wstring();
}

// Dialog file-type filter in the current language: pairs of label + pattern, each
// NUL-terminated, plus a final NUL.
std::wstring DialogFilter()
{
    std::wstring f;
    f += Widen(Tr("Trailpack Memo notes (*.json)")); f += L'\0'; f += L"*.json"; f += L'\0';
    f += Widen(Tr("All files (*.*)"));               f += L'\0'; f += L"*.*";    f += L'\0';
    return f;
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

    ioAction_        = IoAction::Export;
    ioExportId_      = id;
    ioExportGroupId_.clear();
    ioDialog_   = std::async(std::launch::async, RunFileDialog, true,
                             exportDir_.wstring(), Widen(name),
                             Widen(Tr("Export notes")), DialogFilter());
}

void NotesWindow::startExportGroup_(const std::string& groupId)
{
    if (ioDialog_.valid()) return;
    const Magpie::Group* group = store_.GetGroup(groupId);
    if (group == nullptr) return;

    std::error_code ec;
    if (!exportDir_.empty()) fs::create_directories(exportDir_, ec);

    ioAction_        = IoAction::Export;
    ioExportId_.clear();
    ioExportGroupId_ = groupId;
    ioDialog_ = std::async(std::launch::async, RunFileDialog, true,
                           exportDir_.wstring(), Widen(FileNameFor(group->name) + ".json"),
                           Widen(Tr("Export notes")), DialogFilter());
}

void NotesWindow::startImport_()
{
    if (ioDialog_.valid()) return;
    std::error_code ec;
    if (!exportDir_.empty()) fs::create_directories(exportDir_, ec);

    ioAction_ = IoAction::Import;
    ioDialog_ = std::async(std::launch::async, RunFileDialog, false,
                           exportDir_.wstring(), std::wstring(),
                           Widen(Tr("Import notes")), DialogFilter());
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
        std::string json;
        size_t count = 0;
        if (!ioExportGroupId_.empty()) {
            json  = store_.ExportGroupJson(ioExportGroupId_);
            count = std::count_if(store_.Notes().begin(), store_.Notes().end(),
                [&](const Magpie::Note& n) { return n.groupId == ioExportGroupId_; });
        } else {
            std::vector<std::string> ids;
            if (!ioExportId_.empty()) ids.push_back(ioExportId_);
            json  = store_.ExportJson(ids);
            count = ids.empty() ? store_.Notes().size() : 1;
        }

        std::ofstream f(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (f.is_open()) f << json;
        if (!(f.is_open() && f.good())) {
            snprintf(msg, sizeof msg, Tr("Could not write %s."), fileName.c_str());
        } else if (count == 1) {
            snprintf(msg, sizeof msg, Tr("Exported 1 note to %s."), fileName.c_str());
        } else {
            snprintf(msg, sizeof msg, Tr("Exported %d notes to %s."), (int)count, fileName.c_str());
        }
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
                     Tr("%s is not a Trailpack Memo export or notes file.\nNothing was imported."),
                     fileName.c_str());
        } else if (items.empty()) {
            snprintf(msg, sizeof msg, Tr("%s contains no notes."), fileName.c_str());
        } else if (offered == 0) {
            snprintf(msg, sizeof msg,
                     Tr("All notes in %s already exist and are up to date.\nNothing was imported."),
                     fileName.c_str());
        } else {
            // Let the user pick: new notes pre-selected; updates of existing notes are
            // offered but never pre-selected; unchanged duplicates are shown locked.
            importItems_ = std::move(items);
            importChecked_.assign(importItems_.size(), 0);
            for (size_t i = 0; i < importItems_.size(); ++i)
                importChecked_[i] = importItems_[i].duplicate ? 0 : 1;
            importFile_ = fileName;
            ImGui::OpenPopup(PopupName("Import Notes", "import_notes").c_str());
            return;
        }
    }

    ioMessage_ = msg;
    ImGui::OpenPopup(PopupName("Import / Export", "import_export").c_str());
}

void NotesWindow::renderIoModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(PopupName("Import / Export", "import_export").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(ioMessage_.c_str());
        ImGui::Spacing();
        if (ImGui::Button(Tr("OK"), ImVec2(80, 0))) {
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

    if (!ImGui::BeginPopupModal(PopupName("Import Notes", "import_notes").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text(Tr("Notes in %s:"), importFile_.c_str());
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
        const char* title = item.title.empty() ? Tr("(untitled)") : item.title.c_str();
        if (selectable(item)) {
            bool on = importChecked_[i] != 0;
            if (item.duplicate) {
                // Exists locally with different title/text: ask before replacing it.
                char label[300];
                snprintf(label, sizeof label, Tr("Update: %s"), title);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
                if (ImGui::Checkbox(label, &on)) importChecked_[i] = on ? 1 : 0;
                ImGui::PopStyleColor(1);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tr("You already have this note, but the file's version differs.\n"
                                               "Tick to replace your title and text with the file's version."));
                if (on) ++toUpdate;
            } else {
                if (ImGui::Checkbox(title, &on)) importChecked_[i] = on ? 1 : 0;
                if (on) ++toAdd;
            }
        } else if (item.changed) {
            ImGui::TextDisabled(Tr("[-] %s  (open in the editor - save or cancel first)"), title);
        } else {
            // Already in the notes (same id, or same title + text) and identical.
            ImGui::TextDisabled(Tr("[-] %s  (already up to date)"), title);
        }
        if (!item.groupName.empty()) {   // the group it will land in
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", item.groupName.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::SmallButton(Tr("All"))) {
        for (size_t i = 0; i < importItems_.size(); ++i)
            importChecked_[i] = selectable(importItems_[i]) ? 1 : 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(Tr("None"))) {
        std::fill(importChecked_.begin(), importChecked_.end(), 0);
    }
    ImGui::Separator();

    char label[64];
    if (toAdd > 0 && toUpdate > 0)
        snprintf(label, sizeof label, Tr("Import %d, update %d"), toAdd, toUpdate);
    else if (toUpdate > 0)
        snprintf(label, sizeof label, TrN(toUpdate, "Update 1 note", "Update %d notes"), toUpdate);
    else
        snprintf(label, sizeof label, TrN(toAdd, "Import 1 note", "Import %d notes"), toAdd);
    Theme::PushGreenButton();
    const bool doImport = ImGui::Button(label, ImVec2(ButtonWidth(label, 160), 0)) && (toAdd + toUpdate) > 0;
    Theme::PopButton();
    ImGui::SameLine();
    const bool cancel = ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0));

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

// ── Note list rows ──────────────────────────────────────────────────────────

void NotesWindow::renderNoteRow_(const Magpie::Note& note, float indent, std::string& clickedId)
{
    const bool selected = (note.id == selectedId_);
    // Title drawn manually (clipped) so a task-progress count can sit right-aligned
    // without long titles running over it. PushID keeps duplicate titles distinct.
    ImGui::PushID(note.id.c_str());
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##note_row", selected)) {
        clickedId = note.id;
    }
    const float rowRight = ImGui::GetItemRectMax().x;
    const float rowH     = ImGui::GetItemRectSize().y;

    // Drag a note onto a group header (or "No group") to move it.
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("TPM_NOTE", note.id.data(), note.id.size());
        ImGui::TextUnformatted(note.title.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem("##row_ctx")) {
        if (ImGui::MenuItem(Tr("Export note..."))) startExport_(note.id);
        if (!store_.Groups().empty() && ImGui::BeginMenu(Tr("Move to group"))) {
            std::string target;
            bool        move = false;
            for (const auto& g : store_.Groups()) {
                ImGui::PushID(g.id.c_str());
                if (ImGui::MenuItem(g.name.c_str(), nullptr, note.groupId == g.id)) {
                    target = g.id;
                    move   = true;
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr("No group"), nullptr, note.groupId.empty(), !note.groupId.empty()))
                move = true;   // target stays empty -> ungroup
            ImGui::EndMenu();
            if (move && store_.MoveToGroup(note.id, target)) Save();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

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
    dl->AddText(ImVec2(rowPos.x + indent, rowPos.y), ImGui::GetColorU32(ImGuiCol_Text),
                note.title.c_str());
    dl->PopClipRect();

    // Clipped title -> full title on hover.
    if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        rowPos.x + indent + ImGui::CalcTextSize(note.title.c_str()).x > titleRight)
        ImGui::SetTooltip("%s", note.title.c_str());
}

ImVec4 NotesWindow::groupColor_(const Magpie::Group& group) const
{
    if (group.hasColor)
        return ImVec4(group.color[0], group.color[1], group.color[2], group.color[3]);
    const Theme::UserColors& c = Theme::Colors();
    return c.hasGroupName ? c.groupName : c.border;
}

bool NotesWindow::renderGroupHeader_(Magpie::Group& group, bool& openNameModal,
                                     bool& openDeleteModal, bool& openColorModal,
                                     std::string& createInGroup)
{
    ImGui::PushID(group.id.c_str());
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##group_row", false)) {
        group.collapsed = !group.collapsed;
        Save();
    }
    const float rowRight = ImGui::GetItemRectMax().x;
    const float rowH     = ImGui::GetItemRectSize().y;

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("TPM_NOTE")) {
            const std::string id(static_cast<const char*>(p->Data), (size_t)p->DataSize);
            if (store_.MoveToGroup(id, group.id)) {
                group.collapsed = false;   // show where the note went
                Save();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##group_ctx")) {
        if (ImGui::MenuItem(Tr("New note in group"))) createInGroup = group.id;
        if (ImGui::MenuItem(Tr("Rename group..."))) {
            groupNameId_     = group.id;
            groupNameBuffer_ = group.name;
            openNameModal    = true;
        }
        if (ImGui::MenuItem(Tr("Group colour..."))) {
            groupColorId_     = group.id;
            groupColorHadOwn_ = group.hasColor;
            std::copy(group.color, group.color + 4, groupColorBefore_);
            openColorModal    = true;
        }
        if (ImGui::MenuItem(Tr("Export group..."))) startExportGroup_(group.id);
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Delete group..."))) {
            pendingDeleteGroupId_ = group.id;
            openDeleteModal       = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

    // Disclosure triangle (drawn: no symbol glyphs), name, note count right-aligned.
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const float  sz  = ImGui::GetFontSize();
    const ImU32  col = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 c(rowPos.x + sz * 0.4f, rowPos.y + rowH * 0.5f);
    const float  r = sz * 0.25f;
    if (group.collapsed)
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r),
                              ImVec2(c.x + r, c.y), col);
    else
        dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f),
                              ImVec2(c.x, c.y + r), col);

    const int count = (int)std::count_if(store_.Notes().begin(), store_.Notes().end(),
        [&](const Magpie::Note& n) { return n.groupId == group.id; });
    char countText[16];
    snprintf(countText, sizeof countText, "%d", count);
    const float countW = ImGui::CalcTextSize(countText).x;
    dl->AddText(ImVec2(rowRight - countW, rowPos.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                countText);

    const float nameX = rowPos.x + sz * 0.9f;
    dl->PushClipRect(ImVec2(nameX, rowPos.y), ImVec2(rowRight - countW - 6.0f, rowPos.y + rowH), true);
    const char* groupName = group.name.empty() ? Tr("(untitled)") : group.name.c_str();
    dl->AddText(ImVec2(nameX, rowPos.y), ImGui::GetColorU32(groupColor_(group)), groupName);
    dl->PopClipRect();
    if (ImGui::IsItemHovered() && nameX + ImGui::CalcTextSize(groupName).x > rowRight - countW - 6.0f)
        ImGui::SetTooltip("%s", groupName);

    return !group.collapsed;
}

// ── Group modals ────────────────────────────────────────────────────────────

void NotesWindow::renderGroupNameModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(PopupName("Group", "group_name").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted(Tr("Group name:"));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(260.0f);
    const bool enter = ImGui::InputText("##group_name", &groupNameBuffer_,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    Theme::PushGreenButton();
    const bool ok = ImGui::Button(Tr("OK"), ImVec2(80, 0)) || enter;
    Theme::PopButton();
    ImGui::SameLine();
    const bool cancel = ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0));

    if (ok) {
        const std::string name = groupNameBuffer_.empty() ? std::string(Tr("New group"))
                                                          : groupNameBuffer_;
        if (groupNameId_.empty()) {
            store_.CreateGroup(name);
        } else if (Magpie::Group* g = store_.GetGroup(groupNameId_)) {
            g->name = name;
        }
        Save();
    }
    if (ok || cancel) {
        groupNameId_.clear();
        groupNameBuffer_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void NotesWindow::renderDeleteGroupModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(PopupName("Delete Group", "delete_group").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const Magpie::Group* g = store_.GetGroup(pendingDeleteGroupId_);
    ImGui::Text(Tr("Delete the group \"%s\"?"), g != nullptr ? g->name.c_str() : "");
    ImGui::TextUnformatted(Tr("Its notes are kept and moved out of the group."));
    ImGui::Spacing();

    Theme::PushAmberButton();
    const bool del = ImGui::Button(Tr("Delete"), ImVec2(ButtonWidth(Tr("Delete"), 80), 0));
    Theme::PopButton();
    ImGui::SameLine();
    const bool cancel = ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0));

    if (del) {
        store_.DeleteGroup(pendingDeleteGroupId_);
        Save();
    }
    if (del || cancel) {
        pendingDeleteGroupId_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Delete several notes ────────────────────────────────────────────────────

void NotesWindow::renderBulkDeleteModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(PopupName("Delete Notes", "bulk_delete").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto& notes = store_.Notes();
    auto isSelected = [&](const std::string& id) {
        return std::find(bulkSelected_.begin(), bulkSelected_.end(), id) != bulkSelected_.end();
    };
    auto setSelected = [&](const std::string& id, bool on) {
        const auto it = std::find(bulkSelected_.begin(), bulkSelected_.end(), id);
        if (on && it == bulkSelected_.end()) bulkSelected_.push_back(id);
        if (!on && it != bulkSelected_.end()) bulkSelected_.erase(it);
    };
    // Drop ticks of notes that vanished meanwhile (e.g. deleted elsewhere).
    bulkSelected_.erase(std::remove_if(bulkSelected_.begin(), bulkSelected_.end(),
        [&](const std::string& id) { return store_.Get(id) == nullptr; }), bulkSelected_.end());
    const int count = (int)bulkSelected_.size();

    bool close = false;
    if (!bulkConfirm_) {
        ImGui::TextUnformatted(Tr("Select the notes to delete:"));
        ImGui::Spacing();

        // Same layout as the list: each group (its checkbox ticks all of its notes), then
        // ungrouped notes. Scrolls once there are more than ~12 rows.
        const float rowH = ImGui::GetFrameHeightWithSpacing();
        const size_t rows = notes.size() + store_.Groups().size() + 1;
        ImGui::BeginChild("##bulk_list", ImVec2(360.0f, rowH * (float)std::min<size_t>(rows, 12)), false);
        auto noteRow = [&](const Magpie::Note& n) {
            ImGui::PushID(n.id.c_str());
            bool on = isSelected(n.id);
            if (ImGui::Checkbox(n.title.empty() ? Tr("(untitled)") : n.title.c_str(), &on))
                setSelected(n.id, on);
            ImGui::PopID();
        };
        for (const auto& g : store_.Groups()) {
            std::vector<const Magpie::Note*> members;
            for (const auto& n : notes) if (n.groupId == g.id) members.push_back(&n);
            if (members.empty()) continue;
            bool all = std::all_of(members.begin(), members.end(),
                                   [&](const Magpie::Note* n) { return isSelected(n->id); });
            ImGui::PushID(g.id.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, groupColor_(g));
            if (ImGui::Checkbox(g.name.empty() ? Tr("(untitled)") : g.name.c_str(), &all))
                for (const auto* n : members) setSelected(n->id, all);
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Indent();
            for (const auto* n : members) noteRow(*n);
            ImGui::Unindent();
        }
        const bool anyUngrouped = std::any_of(notes.begin(), notes.end(),
            [](const Magpie::Note& n) { return n.groupId.empty(); });
        if (anyUngrouped && !store_.Groups().empty())
            ImGui::TextDisabled("%s", Tr("No group"));
        for (const auto& n : notes)
            if (n.groupId.empty()) noteRow(n);
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::SmallButton(Tr("Select all")))
            for (const auto& n : notes) setSelected(n.id, true);
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr("None"))) bulkSelected_.clear();
        ImGui::Separator();

        char label[64];
        snprintf(label, sizeof label, TrN(count, "Delete 1 note", "Delete %d notes"), count);
        Theme::PushAmberButton();
        if (ImGui::Button(label, ImVec2(ButtonWidth(label, 140), 0)) && count > 0)
            bulkConfirm_ = true;
        Theme::PopButton();
        ImGui::SameLine();
        close = ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0));
    } else {
        // Second step: nothing is deleted until this is confirmed.
        if (count == 1)
            ImGui::TextUnformatted(Tr("Really delete 1 note? This cannot be undone."));
        else
            ImGui::Text(Tr("Really delete %d notes? This cannot be undone."), count);
        ImGui::Spacing();

        Theme::PushAmberButton();
        const bool del = ImGui::Button(Tr("Delete"), ImVec2(ButtonWidth(Tr("Delete"), 80), 0));
        Theme::PopButton();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Back"), ImVec2(ButtonWidth(Tr("Back"), 80), 0))) bulkConfirm_ = false;

        if (del) {
            const bool selectedGone = isSelected(selectedId_);
            if (selectedGone && mode_ == Mode::Edit) cancelEdit_();   // its edit can't be saved
            store_.DeleteMany(bulkSelected_);
            if (selectedGone) selectFirst_();
            Save();
            close = true;
        }
    }

    if (close) {
        bulkSelected_.clear();
        bulkConfirm_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Group colour ────────────────────────────────────────────────────────────

void NotesWindow::renderGroupColorModal_()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(PopupName("Group Colour", "group_color").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    Magpie::Group* g = store_.GetGroup(groupColorId_);
    bool close = (g == nullptr);
    if (g != nullptr) {
        ImGui::TextUnformatted(g->name.empty() ? Tr("(untitled)") : g->name.c_str());
        ImGui::Spacing();

        // Live preview: the group shows the picked colour while the modal is open.
        ImVec4 col = groupColor_(*g);
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::ColorPicker4("##group_color", &col.x,
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview)) {
            g->hasColor = true;
            g->color[0] = col.x; g->color[1] = col.y; g->color[2] = col.z; g->color[3] = col.w;
        }
        ImGui::Spacing();

        Theme::PushGreenButton();
        if (ImGui::Button(Tr("OK"), ImVec2(80, 0))) {
            Save();
            close = true;
        }
        Theme::PopButton();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Use default"), ImVec2(ButtonWidth(Tr("Use default"), 80), 0))) {
            g->hasColor = false;
            Save();
            close = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Use the group colour from the addon options."));
        ImGui::SameLine();
        if (ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 80), 0))) {
            g->hasColor = groupColorHadOwn_;
            std::copy(groupColorBefore_, groupColorBefore_ + 4, g->color);
            close = true;
        }
    }

    if (close) {
        groupColorId_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

