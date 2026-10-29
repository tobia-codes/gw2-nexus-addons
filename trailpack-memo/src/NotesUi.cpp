// NotesUi.cpp — see NotesUi.h.
#include "NotesUi.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include "I18n.h"
#include "Reset.h"
#include "Theme.h"

using Magpie::I18n::Tr;

namespace NotesUi {

float ButtonWidth(const char* label, float minW)
{
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    return w > minW ? w : minW;
}

std::string Ellipsize(const std::string& text, float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    const float dotsW = ImGui::CalcTextSize("...").x;
    if (dotsW > maxWidth) return std::string();
    // Longest prefix (whole UTF-8 characters) that fits together with "...".
    size_t keep = 0;
    for (size_t i = 0; i < text.size();) {
        size_t next = i + 1;
        while (next < text.size() && ((unsigned char)text[next] & 0xC0) == 0x80) ++next;
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + next).x + dotsW > maxWidth) break;
        keep = next;
        i = next;
    }
    return text.substr(0, keep) + "...";
}

const char* TypeName(Magpie::NoteType t)
{
    return t == Magpie::NoteType::Daily  ? Tr("Daily")
         : t == Magpie::NoteType::Weekly ? Tr("Weekly") : Tr("Note");
}

ImVec4 TypeColor(Magpie::NoteType t)
{
    const Theme::UserColors& c = Theme::Colors();
    return t == Magpie::NoteType::Daily ? c.dailyBadge : c.weeklyBadge;
}

ImVec4 TypeTextColor(Magpie::NoteType t)
{
    const Theme::UserColors& c = Theme::Colors();
    return t == Magpie::NoteType::Daily ? c.dailyBadgeText : c.weeklyBadgeText;
}

ImVec4 ListTextColor()
{
    const Theme::UserColors& c = Theme::Colors();
    return c.hasListText ? c.listText : ImGui::GetStyle().Colors[ImGuiCol_Text];
}

ImVec4 NoteTextColor()
{
    const Theme::UserColors& c = Theme::Colors();
    return c.hasNoteText ? c.noteText : ImGui::GetStyle().Colors[ImGuiCol_Text];
}

ImVec4 MenuIconColor()
{
    const Theme::UserColors& c = Theme::Colors();
    return c.hasMenuIcon ? c.menuIcon : ImGui::GetStyle().Colors[ImGuiCol_Text];
}

ImVec4 Dimmed(ImVec4 c)
{
    c.w *= 0.6f;
    return c;
}

std::string ResetText(Magpie::NoteType t)
{
    if (t == Magpie::NoteType::Note) return std::string();
    const int64_t now  = (int64_t)std::time(nullptr);
    const int64_t next = t == Magpie::NoteType::Daily ? Magpie::Reset::NextDaily(now)
                                                      : Magpie::Reset::NextWeekly(now);
    char buf[96];
    snprintf(buf, sizeof buf,
             t == Magpie::NoteType::Daily ? Tr("Daily reset in %s") : Tr("Weekly reset in %s"),
             Magpie::Reset::FormatCountdown(next - now).c_str());
    return buf;
}

float DrawTypeBadge(ImDrawList* dl, ImVec2 pos, float height, Magpie::NoteType t)
{
    if (t == Magpie::NoteType::Note) return 0.0f;
    const char*  letter = t == Magpie::NoteType::Daily ? Tr("D") : Tr("W");
    const ImVec2 ts     = ImGui::CalcTextSize(letter);
    const float  w      = ts.x + ImGui::GetFontSize() * 0.5f;
    const float  pad    = height * 0.08f;
    dl->AddRectFilled(ImVec2(pos.x, pos.y + pad), ImVec2(pos.x + w, pos.y + height - pad),
                      ImGui::GetColorU32(TypeColor(t)), 3.0f);

    // Centre the glyph's ink box, not the line box: a capital sits above the descender
    // space, so centring CalcTextSize would draw it too high.
    ImVec2 textPos(pos.x + (w - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f);
    ImFont* font = ImGui::GetFont();
    if (const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)letter[0])) {
        const float s = ImGui::GetFontSize() / font->FontSize;
        textPos.x = pos.x + (w - (g->X1 - g->X0) * s) * 0.5f - g->X0 * s;
        textPos.y = pos.y + (height - (g->Y1 - g->Y0) * s) * 0.5f - g->Y0 * s;
    }
    textPos = ImVec2(std::floor(textPos.x + 0.5f), std::floor(textPos.y + 0.5f));   // pixel-snap
    dl->AddText(textPos, ImGui::GetColorU32(TypeTextColor(t)), letter);
    return w;
}

} // namespace NotesUi
