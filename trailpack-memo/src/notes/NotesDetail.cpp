// NotesDetail.cpp — NotesWindow's right pane: view and edit modes.

#include "notes/NotesWindow.h"

#include <ctime>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include "ui/FontScale.h"
#include "markdown/MarkdownEdit.h"
#include "markdown/MarkdownRender.h"
#include "notes/NotesUi.h"
#include "events/EventPicker.h"
#include "integrations/CoreClient.h"
#include "events/Events.h"

using Trailpack::I18n::Tr;
using namespace NotesUi;

void NotesWindow::renderDetailPane_()
{
    // Own background (if picked), drawn exactly: the nested panes (view body, editor and
    // preview columns) get a transparent background so no second layer tints it.
    const bool ownBg = Theme::Colors().hasNoteBg;
    if (ownBg) ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors().noteBg);
    Theme::PushNoteBorder();
    ImGui::BeginChild("##notes_detail", ImVec2(0, 0), true, Theme::kPaneFlags);
    Theme::PopPaneBorder();
    if (ownBg) {
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    Trailpack::Note* note = selectedId_.empty() ? nullptr : store_.Get(selectedId_);
    if (note == nullptr) {
        // Empty state.
        ImGui::Spacing();
        const ImVec4 hint = Dimmed(NoteTextColor());
        ImGui::TextColored(hint, "%s", Tr("No notes yet."));
        ImGui::TextColored(hint, "%s", Tr("Click 'Create New Note' to start."));
    } else if (mode_ == Mode::Edit) {
        renderEditMode_(*note);
    } else {
        renderViewMode_(*note);
    }

    if (ownBg) ImGui::PopStyleColor();
    ImGui::EndChild();
}

void NotesWindow::renderEditMode_(Trailpack::Note& note)
{
    // Save / Cancel on the left, Delete on the far right.
    Theme::PushGreenButton();
    if (ImGui::Button(Tr("Save"), ImVec2(ButtonWidth(Tr("Save"), 70), 0))) commitEdit_();
    Theme::PopButton();
    ImGui::SameLine();
    Theme::PushAmberButton();
    if (ImGui::Button(Tr("Cancel"), ImVec2(ButtonWidth(Tr("Cancel"), 70), 0))) cancelEdit_();
    Theme::PopButton();

    const float deleteW = ButtonWidth(Tr("Delete"), 60.0f);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - deleteW);
    Theme::PushAmberButton();
    if (ImGui::Button(Tr("Delete"), ImVec2(deleteW, 0))) deleteRequestId_ = note.id;   // confirm first
    Theme::PopButton();

    ImGui::Separator();
    if (mode_ != Mode::Edit) return;   // Save / Cancel just ended the edit

    // Editable title, with the note's type (applies at once) beside it.
    const float typeW = ButtonWidth(Tr("Weekly"), 60.0f) + ImGui::GetFrameHeight();
    // On a custom note background the fields take a shade of it (FieldBg), so a light
    // theme doesn't put note-coloured text on Nexus's dark field.
    const Theme::UserColors& uc = Theme::Colors();
    const ImVec4 fieldBg = uc.hasNoteBg ? Theme::FieldBg(uc.noteBg) : Theme::EDIT_BG;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, fieldBg);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - typeW - ImGui::GetStyle().ItemSpacing.x);
    {
        Theme::TextColorScope noteText(Theme::Colors().hasNoteText, Theme::Colors().noteText);
        ImGui::InputText("##title", &titleBuffer_);
    }
    Theme::PopFrameBg();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(typeW);
    int type = (int)note.type;
    const char* types[] = { Tr("Note"), Tr("Daily"), Tr("Weekly") };
    bool typeChanged;
    {
        Theme::PopupTextScope popupText;   // the list opens as a popup (and the field is Nexus-dark)
        typeChanged = ImGui::Combo("##type", &type, types, IM_ARRAYSIZE(types));
    }
    if (typeChanged &&
        store_.SetType(note.id, (Trailpack::NoteType)type, (int64_t)std::time(nullptr)))
        Save();
    if (ImGui::IsItemHovered())
        Theme::Tooltip("%s", Tr("Daily / Weekly: checkboxes are unchecked after the reset."));

    ImGui::Spacing();
    renderEditToolbar_();

    // Editable body: chat codes are atomic chip cells, markdown stays raw (edit = source).
    // Editor frame: off by default (0 = no frame), colour follows the window border until picked.
    const Theme::UserColors& ec = Theme::Colors();
    const ImU32 border = !ec.showEditorBorder ? 0u
                       : ImGui::GetColorU32(ec.hasEditorBorder ? ec.editorBorder : ec.border);
    if (!showPreview_) {
        Trailpack::FontScaleScope noteFont(noteTextScale_);
        Theme::TextColorScope noteText(uc.hasNoteText, uc.noteText);
        if (uc.hasNoteBg) ImGui::PushStyleColor(ImGuiCol_FrameBg, fieldBg);
        bodyEditor_.Render("##body_edit", ImGui::GetContentRegionAvail().x, border, /*minLines=*/6);
        if (uc.hasNoteBg) ImGui::PopStyleColor();
        renderEventSuggest_();
        return;
    }

    // Source on the left, live rendered preview on the right.
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginChild("##edit_col", ImVec2(half, 0), false);
    {
        Trailpack::FontScaleScope noteFont(noteTextScale_);
        Theme::TextColorScope noteText(uc.hasNoteText, uc.noteText);
        if (uc.hasNoteBg) ImGui::PushStyleColor(ImGuiCol_FrameBg, fieldBg);
        bodyEditor_.Render("##body_edit", ImGui::GetContentRegionAvail().x, border, /*minLines=*/6);
        if (uc.hasNoteBg) ImGui::PopStyleColor();
    }
    renderEventSuggest_();
    ImGui::EndChild();

    ImGui::SameLine();
    Theme::PushNoteBorder();   // the preview pane is framed like the note
    ImGui::BeginChild("##preview_col", ImVec2(0, 0), true, Theme::kPaneFlags);
    Theme::PopPaneBorder();
    {
        const std::vector<Trailpack::Md::Line>& lines = parsed_(previewParse_, bodyEditor_.GetText());
        Trailpack::FontScaleScope noteFont(noteTextScale_);
        Theme::TextColorScope noteText(uc.hasNoteText, uc.noteText);
        Trailpack::RenderMarkdown(lines, fontBig_(), ImGui::GetContentRegionAvail().x);
    }
    ImGui::EndChild();
}

void NotesWindow::renderViewMode_(Trailpack::Note& note)
{
    // Title (+ type and reset countdown), Edit on the right. Deleting lives in the
    // right-click menus and "Delete notes...". Title and countdown share the space left of
    // Edit and are cut with "..." when it runs out (full text in the tooltip).
    const float editW   = ButtonWidth(Tr("Edit"), 60.0f);
    const float rowX    = ImGui::GetCursorPosX();
    const float rowW    = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float textW   = rowW - editW - spacing;
    const std::string reset = note.type != Trailpack::NoteType::Note ? "[" + ResetText(note.type) + "]" : "";

    const std::string title = Ellipsize(note.title, textW);
    bool cut = title != note.title;
    {
        Theme::TextColorScope noteText(Theme::Colors().hasNoteText, Theme::Colors().noteText);
        ImGui::TextUnformatted(title.c_str());
    }
    bool hovered = ImGui::IsItemHovered();
    if (!reset.empty() && !cut) {
        const float left = textW - ImGui::CalcTextSize(title.c_str()).x - spacing;
        const std::string shown = Ellipsize(reset, left);
        cut = shown != reset;
        if (!shown.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(TypeColor(note.type), "%s", shown.c_str());
            hovered = hovered || ImGui::IsItemHovered();
        }
    }
    if (cut && hovered) Theme::Tooltip("%s%s%s", note.title.c_str(), reset.empty() ? "" : "\n", reset.c_str());

    ImGui::SameLine(rowX + rowW - editW);
    Theme::PushGreenButton();
    if (ImGui::Button(Tr("Edit"), ImVec2(editW, 0))) enterEdit_(&note);
    Theme::PopButton();

    ImGui::Separator();
    if (mode_ == Mode::View) renderViewBody_(&note);
}

void NotesWindow::renderEditToolbar_()
{
    using namespace Trailpack::Md;

    // The buttons flow like words: each stays on the current row if it fits, else starts a new
    // one, so a narrow pane wraps the toolbar instead of cutting it off.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool firstItem = true;
    auto fitsOnRow = [&](float w) {
        return !firstItem && ImGui::GetItemRectMax().x + style.ItemSpacing.x + w <= rowRight;
    };

    // Labels stay ASCII: Nexus's font has no symbol glyphs.
    auto toolButton = [&](const char* label, const char* tip) {
        if (fitsOnRow(ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f)) ImGui::SameLine();
        firstItem = false;
        const bool pressed = ImGui::Button(label);
        if (ImGui::IsItemHovered()) Theme::Tooltip("%s", tip);
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

    // Event picker (D8): a popup in this child's id scope, begun every frame below. Event data
    // comes from Trailpack Core: without it the button is greyed out (Core PLAN D17).
    const bool haveCore = Trailpack::CoreClient::Present();
    {
        Theme::LockedScope locked(!haveCore);
        if (toolButton(Tr("Event"), Tr("Insert an event timer")) && haveCore) {
            eventQuery_.clear();
            eventPickerFocus_ = true;
            ImGui::OpenPopup("##event_picker");
        }
    }
    if (!haveCore && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        Theme::Tooltip("%s", Tr("Event timers are unlocked with the Trailpack Core addon."));

    // Preview toggle, right-aligned on the last button row if it fits there, else below.
    const char* previewLabel = Tr("Preview");
    const float boxW = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                       ImGui::CalcTextSize(previewLabel).x;
    if (fitsOnRow(boxW)) {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(rowRight - boxW, ImGui::GetCursorScreenPos().y));
    }
    bool toggled;
    {
        // The label sits on the note background: note text colour (the tooltip below doesn't).
        Theme::TextColorScope noteText(Theme::Colors().hasNoteText, Theme::Colors().noteText);
        toggled = ImGui::Checkbox(previewLabel, &showPreview_);
    }
    if (toggled) settingsChanged_ = true;
    if (ImGui::IsItemHovered()) Theme::Tooltip("%s", Tr("Show a live preview beside the editor"));

    // The picked event becomes a chip at the editor's caret (the selection stb kept when the
    // button took the focus); the editor gets the focus back.
    if (ImGui::BeginPopup("##event_picker")) {
        Theme::PopupTextScope popupText;
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) ImGui::CloseCurrentPopup();
        const std::string id = Trailpack::EventPicker::Render(eventQuery_, eventPickerFocus_);
        if (!id.empty()) {
            bodyEditor_.InsertChip(Trailpack::Events::Token(id));
            bodyEditor_.Focus();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Inline "/e " suggestions (D9) below the editor's caret. The editor routes Up / Down / Enter /
// Escape here while rows are shown; Enter or a click replaces the typed "/e query" with the
// event chip. The popup is begun in the same id scope as the editor, right after it.
void NotesWindow::renderEventSuggest_()
{
    using namespace Trailpack;
    ChipTextEdit::Suggest& sg = bodyEditor_.suggest;

    // Without Trailpack Core there are no events: no list, and the editor keeps its keys.
    std::vector<const CoreClient::Event*> hits;
    if (sg.active) {
        hits = CoreClient::SearchEvents(sg.query);
        if ((int)hits.size() > EventPicker::kMaxSuggestions) hits.resize(EventPicker::kMaxSuggestions);
    }
    if (!sg.active || sg.query != eventSuggestQuery_) {
        eventSuggestSel_   = 0;
        eventSuggestQuery_ = sg.query;
    }
    const int n = (int)hits.size();
    if (n > 0) eventSuggestSel_ = (eventSuggestSel_ + sg.move + n) % n;

    std::string picked;
    if (sg.accept && n > 0) picked = hits[eventSuggestSel_]->id;

    bodyEditor_.suggestCount = 0;
    if (n > 0 && picked.empty()) {
        if (!ImGui::IsPopupOpen("##event_suggest")) ImGui::OpenPopup("##event_suggest");
        ImGui::SetNextWindowPos(sg.pos);
        if (ImGui::BeginPopup("##event_suggest", ImGuiWindowFlags_NoNav)) {
            Theme::PopupTextScope popupText;
            picked = EventPicker::RenderSuggestions(hits, eventSuggestSel_);
            bodyEditor_.suggestCount = n;
            bodyEditor_.suggestMin = ImGui::GetWindowPos();
            bodyEditor_.suggestMax = ImVec2(bodyEditor_.suggestMin.x + ImGui::GetWindowWidth(),
                                            bodyEditor_.suggestMin.y + ImGui::GetWindowHeight());
            ImGui::EndPopup();
        }
    } else if (ImGui::IsPopupOpen("##event_suggest")) {
        // No public close-by-id in this ImGui: begin it once more, off screen, and close it.
        ImGui::SetNextWindowPos(ImVec2(-10000.0f, -10000.0f));
        if (ImGui::BeginPopup("##event_suggest", ImGuiWindowFlags_NoNav)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (!picked.empty()) {
        bodyEditor_.ReplaceWithChip(sg.start, sg.end, Events::Token(picked));
        bodyEditor_.Focus();
        sg.active = false;
    }
}

void NotesWindow::renderViewBody_(Trailpack::Note* note)
{
    // Rendered markdown inside a scrollable child so long notes scroll.
    // EDIT mode shows the raw source; VIEW shows this.
    ImGui::BeginChild("##view_body", ImVec2(0, 0), false);

    const std::vector<Trailpack::Md::Line>& lines = parsed_(viewParse_, note->body);
    int clickedTask = -1;
    {
        Trailpack::FontScaleScope noteFont(noteTextScale_);
        Theme::TextColorScope noteText(Theme::Colors().hasNoteText, Theme::Colors().noteText);
        clickedTask = Trailpack::RenderMarkdown(lines, fontBig_(),
                                             ImGui::GetContentRegionAvail().x,
                                             /*interactiveTasks=*/true);
    }
    if (clickedTask >= 0 && Trailpack::Md::ToggleTask(note->body, clickedTask)) {
        Save();
    }

    // Right-click on the body (but not on a chip, which opens its own menu
    // during RenderMarkdown) -> note context menu.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::OpenPopup("##view_ctx");
    }
    if (ImGui::BeginPopup("##view_ctx")) {
        Theme::PopupTextScope popupText;
        if (ImGui::MenuItem(Tr("Edit"))) editRequestId_ = note->id;
        ImGui::Separator();
        const Trailpack::Md::TaskCount tc = Trailpack::Md::CountTasks(note->body);
        if (ImGui::MenuItem(Tr("Uncheck all tasks"), nullptr, false, tc.done > 0)) {
            Trailpack::Md::UncheckAllTasks(note->body);
            Save();
        }
        typeMenu_(note->id);
        if (ImGui::MenuItem(Tr("Export note..."))) startExport_(note->id);
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("Delete note..."))) deleteRequestId_ = note->id;
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}
