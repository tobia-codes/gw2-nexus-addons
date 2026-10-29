// NotesImportExport.cpp — NotesWindow's export / import: file dialogs and the import
// selection modal.

#include "NotesWindow.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include "imgui.h"
#include "Theme.h"
#include "I18n.h"
#include "NotesUi.h"
#include "FileDialog.h"

namespace fs = std::filesystem;
using Magpie::I18n::Tr;
using Magpie::I18n::TrN;
using namespace NotesUi;
using FileDialog::Widen;

namespace {

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

// Dialog file-type filter for note files, in the current language.
std::wstring DialogFilter()
{
    return FileDialog::JsonFilter(Tr("Trailpack Memo notes (*.json)"));
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
    ioDialog_   = std::async(std::launch::async, FileDialog::Run, true,
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
    ioDialog_ = std::async(std::launch::async, FileDialog::Run, true,
                           exportDir_.wstring(), Widen(FileNameFor(group->name) + ".json"),
                           Widen(Tr("Export notes")), DialogFilter());
}

void NotesWindow::startImport_()
{
    if (ioDialog_.valid()) return;
    std::error_code ec;
    if (!exportDir_.empty()) fs::create_directories(exportDir_, ec);

    ioAction_ = IoAction::Import;
    ioDialog_ = std::async(std::launch::async, FileDialog::Run, false,
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
            requestPopup_(Popup::ImportNotes);
            return;
        }
    }

    ioMessage_ = msg;
    requestPopup_(Popup::IoResult);
}

void NotesWindow::renderIoModal_()
{
    if (beginModal_(Popup::IoResult))
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
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    if (!beginModal_(Popup::ImportNotes))
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
                    Theme::Tooltip("%s", Tr("You already have this note, but the file's version differs.\n"
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
