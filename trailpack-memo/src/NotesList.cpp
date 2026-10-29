// NotesList.cpp — NotesWindow's left column: groups, note rows, menu button, splitter.

#include "NotesWindow.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include "imgui.h"
#include "Theme.h"
#include "I18n.h"
#include "NotesUi.h"

using Magpie::I18n::Tr;
using namespace NotesUi;

// Hamburger icon drawn with lines over the last item: Nexus's font has no symbol
// glyphs, and it reads the same in every language.
static void DrawHamburger(float size)
{
    const ImVec2 bMin = ImGui::GetItemRectMin();
    const ImVec2 bMax = ImGui::GetItemRectMax();
    const float  padX = size * 0.25f;
    const float  gap  = size * 0.18f;
    const float  midY = (bMin.y + bMax.y) * 0.5f;
    const ImU32  col  = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    for (int i = -1; i <= 1; ++i) {
        const float y = midY + i * gap;
        dl->AddLine(ImVec2(bMin.x + padX, y), ImVec2(bMax.x - padX, y), col, 1.5f);
    }
}

// ── Column ────────────────────────────────────────────────────────────────

float NotesWindow::renderListColumn_()
{
    const auto& notes = store_.Notes();

    // The list width is user-resizable (splitter); keep room for the detail pane.
    // Clamped for drawing only: the saved width survives a temporarily narrow window.
    const float minList   = 120.0f;
    const float maxList   = ImGui::GetContentRegionAvail().x - 200.0f;
    const float leftWidth = maxList > minList
        ? (listWidth_ < minList ? minList : listWidth_ > maxList ? maxList : listWidth_)
        : minList;
    const float buttonRowH = ImGui::GetFrameHeightWithSpacing();

    // Grouped so the right-hand pane (SameLine) sits beside the FULL column height.
    ImGui::BeginGroup();

    // The list fills the column minus the button row (negative height = fill minus px).
    Theme::PushDarkFrameBg();
    ImGui::BeginChild("##notes_list", ImVec2(leftWidth, -buttonRowH), true);
    Theme::PopFrameBg();

    // Groups first (each followed by its notes unless collapsed), then ungrouped notes.
    // Ids are snapshotted: a header's context menu may change the store this frame.
    const float groupIndent = ImGui::GetFontSize() * 0.9f;
    std::vector<std::string> groupIds;
    for (const auto& g : store_.Groups()) groupIds.push_back(g.id);
    for (const auto& gid : groupIds) {
        Magpie::Group* group = store_.GetGroup(gid);
        if (group == nullptr || !renderGroupHeader_(*group)) continue;
        for (const auto& note : notes)
            if (note.groupId == gid) renderNoteRow_(note, groupIndent);
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
        if (note.groupId.empty()) renderNoteRow_(note, 0.0f);

    ImGui::EndChild();

    // Bottom row: "Create New Note" + a square menu button.
    const float menuW   = ImGui::GetFrameHeight();
    const float createW = leftWidth - menuW - ImGui::GetStyle().ItemSpacing.x;
    Theme::PushGreenButton();
    if (ImGui::Button(Tr("Create New Note"), ImVec2(createW, 0))) createRequested_ = true;
    Theme::PopButton();

    ImGui::SameLine();
    if (ImGui::Button("##notes_menu_btn", ImVec2(menuW, menuW))) ImGui::OpenPopup("##notes_menu");
    DrawHamburger(menuW);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Menu"));

    if (ImGui::BeginPopup("##notes_menu")) {
        if (ImGui::MenuItem(Tr("New group..."))) {
            groupNameId_.clear();
            groupNameBuffer_ = Tr("New group");
            requestPopup_(Popup::GroupName);
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
            bulkConfirm_ = false;
            requestPopup_(Popup::BulkDelete);
        }
        ImGui::EndPopup();
    }

    ImGui::EndGroup();
    return leftWidth;
}

// Thin draggable divider between list and detail pane; double-click resets the width.
void NotesWindow::renderSplitter_(float listWidth)
{
    const float splitW = 6.0f;
    const float gap    = ImGui::GetStyle().ItemSpacing.x;
    const float side   = (gap - splitW) * 0.5f > 0.0f ? (gap - splitW) * 0.5f : 0.0f;
    ImGui::SameLine(0.0f, side);
    ImGui::InvisibleButton("##list_splitter", ImVec2(splitW, ImGui::GetContentRegionAvail().y));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active) listWidth_ = listWidth + ImGui::GetIO().MouseDelta.x;
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
    ImGui::SameLine(0.0f, side);   // the detail pane follows on this line
}

// ── Rows ──────────────────────────────────────────────────────────────────

void NotesWindow::renderNoteRow_(const Magpie::Note& note, float indent)
{
    const bool selected = (note.id == selectedId_);
    // Title drawn manually (clipped) so a task-progress count can sit right-aligned
    // without long titles running over it. PushID keeps duplicate titles distinct.
    ImGui::PushID(note.id.c_str());
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##note_row", selected)) {
        selectRequestId_ = note.id;
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
        if (ImGui::MenuItem(Tr("Edit"), nullptr, false,
                            !(mode_ == Mode::Edit && selectedId_ == note.id)))
            editRequestId_ = note.id;
        ImGui::Separator();
        typeMenu_(note.id);
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
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Delete note..."))) deleteRequestId_ = note.id;
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    char progress[24] = "";
    float progressW = 0.0f;
    const Magpie::Md::TaskCount tc = taskCount_(note);
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
    const float badgeW = DrawTypeBadge(dl, ImVec2(rowPos.x + indent, rowPos.y), rowH, note.type);
    const float titleX = rowPos.x + indent + (badgeW > 0.0f ? badgeW + 4.0f : 0.0f);
    dl->AddText(ImVec2(titleX, rowPos.y), ImGui::GetColorU32(ImGuiCol_Text), note.title.c_str());
    dl->PopClipRect();

    // Hover: full title when clipped, and the reset countdown for daily/weekly lists.
    if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const bool clipped = titleX + ImGui::CalcTextSize(note.title.c_str()).x > titleRight;
        const std::string reset = ResetText(note.type);
        if (clipped && !reset.empty())
            ImGui::SetTooltip("%s\n%s", note.title.c_str(), reset.c_str());
        else if (clipped)
            ImGui::SetTooltip("%s", note.title.c_str());
        else if (!reset.empty())
            ImGui::SetTooltip("%s", reset.c_str());
    }
}

ImVec4 NotesWindow::groupColor_(const Magpie::Group& group) const
{
    if (group.hasColor)
        return ImVec4(group.color[0], group.color[1], group.color[2], group.color[3]);
    const Theme::UserColors& c = Theme::Colors();
    return c.hasGroupName ? c.groupName : c.border;
}

bool NotesWindow::renderGroupHeader_(Magpie::Group& group)
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
        if (ImGui::MenuItem(Tr("New note in group"))) {
            createRequested_      = true;
            createRequestGroupId_ = group.id;
        }
        if (ImGui::MenuItem(Tr("Rename group..."))) {
            groupNameId_     = group.id;
            groupNameBuffer_ = group.name;
            requestPopup_(Popup::GroupName);
        }
        if (ImGui::MenuItem(Tr("Group colour..."))) {
            groupColorId_     = group.id;
            groupColorHadOwn_ = group.hasColor;
            std::copy(group.color, group.color + 4, groupColorBefore_);
            requestPopup_(Popup::GroupColor);
        }
        if (ImGui::MenuItem(Tr("Export group..."))) startExportGroup_(group.id);
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Delete group..."))) {
            pendingDeleteGroupId_ = group.id;
            requestPopup_(Popup::DeleteGroup);
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

void NotesWindow::typeMenu_(const std::string& noteId)
{
    const Magpie::Note* n = store_.Get(noteId);
    if (n == nullptr || !ImGui::BeginMenu(Tr("Type"))) return;
    const Magpie::NoteType types[] = { Magpie::NoteType::Note, Magpie::NoteType::Daily,
                                       Magpie::NoteType::Weekly };
    for (Magpie::NoteType t : types) {
        if (ImGui::MenuItem(TypeName(t), nullptr, n->type == t) &&
            store_.SetType(noteId, t, (int64_t)std::time(nullptr)))
            Save();
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", Tr("Daily / Weekly: checkboxes are unchecked after the reset."));
    ImGui::EndMenu();
}
