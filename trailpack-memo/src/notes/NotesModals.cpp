// NotesModals.cpp — NotesWindow's modal dialogs (opened via requestPopup_).

#include "notes/NotesWindow.h"

#include <algorithm>
#include <cstdio>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include "notes/NotesUi.h"

using Trailpack::I18n::Tr;
using Trailpack::I18n::TrN;
using namespace NotesUi;

void NotesWindow::renderModals_()
{
    // Only modals are drawn here: they all get the popup text colour (background: PopupBg,
    // pushed with the window style).
    Theme::PopupTextScope popupText;
    renderUnsavedModal_();
    renderDeleteModal_();
    renderNoticeModal_();
    renderIoModal_();
    renderImportModal_();
    renderGroupNameModal_();
    renderDeleteGroupModal_();
    renderGroupColorModal_();
    renderBulkDeleteModal_();
}

// ── Confirmations ─────────────────────────────────────────────────────────

void NotesWindow::renderUnsavedModal_()
{
    if (beginModal_(Popup::Unsaved))
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
            pendingEditId_.clear();
            pendingCreate_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void NotesWindow::renderDeleteModal_()
{
    if (beginModal_(Popup::ConfirmDelete))
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

void NotesWindow::renderNoticeModal_()
{
    if (!beginModal_(Popup::Notice))
        return;
    ImGui::TextUnformatted(noticeMessage_.c_str());
    ImGui::Spacing();
    if (ImGui::Button(Tr("OK"), ImVec2(80, 0))) {
        noticeMessage_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Group modals ────────────────────────────────────────────────────────────

void NotesWindow::renderGroupNameModal_()
{
    if (!beginModal_(Popup::GroupName))
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
        } else if (Trailpack::Group* g = store_.GetGroup(groupNameId_)) {
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
    if (!beginModal_(Popup::DeleteGroup))
        return;

    const Trailpack::Group* g = store_.GetGroup(pendingDeleteGroupId_);
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
    if (!beginModal_(Popup::BulkDelete))
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
        auto noteRow = [&](const Trailpack::Note& n) {
            ImGui::PushID(n.id.c_str());
            bool on = isSelected(n.id);
            if (ImGui::Checkbox(n.title.empty() ? Tr("(untitled)") : n.title.c_str(), &on))
                setSelected(n.id, on);
            ImGui::PopID();
        };
        for (const auto& g : store_.Groups()) {
            std::vector<const Trailpack::Note*> members;
            for (const auto& n : notes) if (n.groupId == g.id) members.push_back(&n);
            if (members.empty()) continue;
            bool all = std::all_of(members.begin(), members.end(),
                                   [&](const Trailpack::Note* n) { return isSelected(n->id); });
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
            [](const Trailpack::Note& n) { return n.groupId.empty(); });
        if (anyUngrouped && !store_.Groups().empty()) ImGui::Spacing();
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
    if (!beginModal_(Popup::GroupColor))
        return;

    Trailpack::Group* g = store_.GetGroup(groupColorId_);
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
            Theme::Tooltip("%s", Tr("Use the group colour from the addon options."));
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
