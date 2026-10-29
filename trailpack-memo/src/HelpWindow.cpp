// HelpWindow.cpp — see HelpWindow.h. All text goes through Tr (German in I18n.cpp).
#include "HelpWindow.h"

#include <string>
#include "imgui.h"
#include "I18n.h"
#include "Theme.h"
#include "FontScale.h"

using Magpie::I18n::Tr;

namespace Help {

void TitleBarButton(const char* windowTitle, bool* open)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 winPos  = ImGui::GetWindowPos();
    const float  titleH  = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
    const float  size    = ImGui::GetFontSize();

    // Title text starts after the collapse arrow (arrow = font size + inner spacing).
    const float textX = winPos.x + style.FramePadding.x + ImGui::GetFontSize() +
                        style.ItemInnerSpacing.x;
    const ImVec2 pos(textX + ImGui::CalcTextSize(windowTitle).x + style.ItemInnerSpacing.x * 2.0f,
                     winPos.y + (titleH - size) * 0.5f);

    // Items in the title bar: clip to the whole window, keep the content cursor intact.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(winPos, ImVec2(winPos.x + ImGui::GetWindowWidth(), winPos.y + titleH), false);
    ImGui::SetCursorScreenPos(pos);
    if (ImGui::InvisibleButton("##help_btn", ImVec2(size, size))) *open = !*open;
    const bool hovered = ImGui::IsItemHovered();

    // Drawn, not a glyph button: a ring with "?" in the border colour.
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    const ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
    const ImU32  col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    if (hovered || *open)
        dl->AddCircleFilled(c, size * 0.5f, ImGui::GetColorU32(Theme::Colors().border), 20);
    dl->AddCircle(c, size * 0.5f - 0.5f, col, 20, 1.5f);
    const ImVec2 q = ImGui::CalcTextSize("?");
    dl->AddText(ImVec2(c.x - q.x * 0.5f, c.y - q.y * 0.5f), col, "?");
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(cursor);

    if (hovered) ImGui::SetTooltip("%s", Tr("Guide"));
}

namespace {

// English text (= translation key) of a section: title + body. Body lines starting with
// "- " are shown as bullets.
struct Section { const char* title; const char* body; };

const Section kSections[] = {
    { "Getting started",
      "Trailpack Memo keeps notes and checklists next to the game.\n"
      "- Open / close the window with CTRL+SHIFT+T or the quick-access icon.\n"
      "- Click 'Create New Note', type a title and text, then 'Save'.\n"
      "- Click a note in the list to show it, 'Edit' (or right-click > 'Edit') to change it." },
    { "Writing notes",
      "Notes use simple markdown; the edit view shows the raw text.\n"
      "- **bold**, *italic*, # Heading, - bullet list\n"
      "- Toolbar buttons insert or remove these for the selection; CTRL+B / CTRL+I too.\n"
      "- Enter in a list continues it; Enter on an empty item ends it.\n"
      "- 'Preview' shows the rendered note beside the editor." },
    { "Checklists",
      "- A line '- [ ] task' becomes a checkbox; '- [x] task' is ticked.\n"
      "- Click a box in the note view to tick it - saved immediately.\n"
      "- The list shows progress (3/7, green when complete).\n"
      "- Right-click the note > 'Uncheck all tasks' to start over." },
    { "Daily and weekly lists",
      "Give a note a type: right-click > 'Type', or the selector next to the title while editing.\n"
      "- Daily: all boxes are unticked after the daily reset (00:00 UTC).\n"
      "- Weekly: after the weekly reset (Monday 07:30 UTC).\n"
      "- A coloured D / W badge marks them; hover it for the time to the next reset.\n"
      "- Missed resets (game closed) are caught up when the window is opened." },
    { "Groups",
      "- Menu button (next to 'Create New Note') > 'New group...'.\n"
      "- Click a group to fold / unfold it. Drag notes onto it, or right-click a note > "
      "'Move to group'.\n"
      "- Right-click a group: new note in it, rename, colour, export, delete (its notes are kept)." },
    { "Chat links",
      "Paste GW2 chat links ([&...]) into a note - they become chips.\n"
      "- With the Decoder Ring addon: icon, name and a detailed tooltip.\n"
      "- Right-click a chip: copy the chat code or open the wiki.\n"
      "- With Pie UI: left-click a waypoint chip to open the map there." },
    { "Event timers",
      "Write {{event:<id>}} in a note, e.g. {{event:tequatl-the-sunless}} - it becomes an event "
      "chip.\n"
      "- It shows the time until the next start, or since when the event is running.\n"
      "- Hover it for the next starts in your local time, the duration, map and waypoint.\n"
      "- Left-click copies the waypoint; right-click: copy waypoint or open the wiki.\n"
      "- While editing, the chip shows the event name only." },
    { "Import, export and deleting",
      "All in the menu button and the right-click menus.\n"
      "- 'Export all...' saves every note and group to one file; right-click a note or group "
      "to export just that.\n"
      "- 'Import...' lists the notes in a file to pick from. Notes you already have are skipped; "
      "a changed version can be applied with 'Update'.\n"
      "- 'Delete notes...' deletes several notes at once; right-click > 'Delete note...' "
      "deletes one." },
    { "Settings",
      "Nexus addon options > Trailpack Memo, or menu button > 'Settings...':\n"
      "- Language (Auto follows Nexus), quick access icon.\n"
      "- 'Font options': UI and note text size.\n"
      "- 'Colour options': window (border on/off and colour, background, text, menu button and icon), note list (border on/off and colour, background, text, group names, 'No group'), note (border on/off and colour, background, text) and the daily / weekly badges.\n"
      "- 'Colour options' > Theme: Default, Light, Dark or one of your own; 'Save as theme' "
      "stores the current colours under a name. Borders are off by default.\n"
      "- 'Backup': 'Create backup...' saves all notes, groups and settings in one file; "
      "'Restore backup...' replaces them with a backup's after you confirm.\n"
      "- Drag the divider between list and note to change the list width." },
    { "Your data",
      "Notes are saved in addons/TrailpackMemo/notes.json; the previous version is kept as "
      "notes.json.bak. A damaged file is never overwritten - it is kept aside and the backup "
      "is loaded instead." },
};

void DrawBody(const char* body)
{
    // Line by line: "- " lines get a bullet, everything wraps to the window.
    const std::string text = Tr(body);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        if (line.compare(0, 2, "- ") == 0) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", line.c_str() + 2);
        } else if (!line.empty()) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        start = end + 1;
    }
}

} // namespace

std::string Title()
{
    return std::string("Trailpack Memo - ") + Tr("Guide") + "###tpm_help";
}

void Render(bool* open, float uiScale)
{
    if (!*open) return;

    const std::string title = Title();
    ImGui::SetNextWindowSize(ImVec2(460.0f, 520.0f), ImGuiCond_FirstUseEver);
    // Plain Nexus style (no user colours), so the guide stays readable with any theme.
    if (ImGui::Begin(title.c_str(), open)) {
        Magpie::FontScaleScope uiFont(uiScale);
        for (size_t i = 0; i < sizeof kSections / sizeof kSections[0]; ++i) {
            ImGui::PushID((int)i);
            if (ImGui::CollapsingHeader(Tr(kSections[i].title), i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                DrawBody(kSections[i].body);
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

} // namespace Help
