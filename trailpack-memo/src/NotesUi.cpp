// NotesUi.cpp — see NotesUi.h.
#include "NotesUi.h"

#include <cstdio>
#include <ctime>
#include "I18n.h"
#include "Reset.h"

using Magpie::I18n::Tr;

namespace NotesUi {

float ButtonWidth(const char* label, float minW)
{
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    return w > minW ? w : minW;
}

const char* TypeName(Magpie::NoteType t)
{
    return t == Magpie::NoteType::Daily  ? Tr("Daily")
         : t == Magpie::NoteType::Weekly ? Tr("Weekly") : Tr("Note");
}

ImVec4 TypeColor(Magpie::NoteType t)
{
    return t == Magpie::NoteType::Daily ? ImVec4(0.90f, 0.62f, 0.20f, 1.0f)    // amber
                                        : ImVec4(0.40f, 0.60f, 0.95f, 1.0f);   // blue
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
    dl->AddText(ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
                IM_COL32(20, 20, 20, 255), letter);
    return w;
}

} // namespace NotesUi
