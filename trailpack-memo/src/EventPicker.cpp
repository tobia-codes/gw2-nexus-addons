// EventPicker.cpp — see EventPicker.h.
#include "EventPicker.h"

#include <vector>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "Events.h"
#include "I18n.h"

namespace Magpie { namespace EventPicker {

using I18n::Tr;

std::string Render(std::string& query, bool& focusSearch)
{
    const float fs = ImGui::GetFontSize();
    std::string picked;

    // Search: Enter takes the first hit.
    if (focusSearch) {
        ImGui::SetKeyboardFocusHere();
        focusSearch = false;
    }
    ImGui::SetNextItemWidth(fs * 22.0f);
    const bool enter = ImGui::InputTextWithHint("##event_search", Tr("Search events..."), &query,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    const bool searching = !query.empty();
    const std::vector<const Events::EventDef*> hits = Events::Search(query);
    if (enter && !hits.empty()) picked = hits.front()->id;

    // Events by category (wiki order). The list has a fixed height and scrolls.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));   // popup bg shows as is
    ImGui::BeginChild("##event_list", ImVec2(fs * 22.0f, fs * 18.0f), false);
    ImGui::PopStyleColor();
    if (hits.empty()) ImGui::TextDisabled("%s", Tr("No events found."));
    const I18n::Lang lang = I18n::Current();
    for (uint8_t cat = 0; cat < Events::kCategoryCount; ++cat) {
        bool any = false;
        for (const Events::EventDef* e : hits) any = any || e->category == cat;
        if (!any) continue;

        ImGui::PushID(cat);
        if (searching) ImGui::SetNextItemOpen(true, ImGuiCond_Always);   // hits always visible
        if (ImGui::CollapsingHeader(Events::CategoryName(cat, lang))) {
            for (const Events::EventDef* e : hits) {
                if (e->category != cat) continue;
                // A full-width selectable row with the name and (grey) map drawn over it.
                ImGui::PushID(e->id);
                const float x = ImGui::GetCursorPosX();
                if (ImGui::Selectable("##row")) picked = e->id;
                ImGui::SameLine(x);
                ImGui::TextUnformatted(Events::Name(*e, lang));
                const std::string maps = Events::Maps(*e, lang);   // all places of a rotating event
                if (!maps.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", maps.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    return picked;
}

}} // namespace Magpie::EventPicker
