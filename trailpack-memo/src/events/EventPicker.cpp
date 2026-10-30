// EventPicker.cpp — see EventPicker.h.
#include "events/EventPicker.h"

#include <algorithm>
#include <vector>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/I18n.h"
#include "integrations/CoreClient.h"

namespace Trailpack { namespace EventPicker {

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
    const std::vector<const CoreClient::Event*> hits = CoreClient::SearchEvents(query);
    if (enter && !hits.empty()) picked = hits.front()->id;

    // Events by category (wiki order). The list has a fixed height and scrolls.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));   // popup bg shows as is
    ImGui::BeginChild("##event_list", ImVec2(fs * 22.0f, fs * 18.0f), false);
    ImGui::PopStyleColor();
    if (hits.empty()) ImGui::TextDisabled("%s", Tr("No events found."));
    // Categories in data (wiki) order: the order they first appear in.
    std::vector<std::string> categories;
    for (const CoreClient::Event& e : CoreClient::Events())
        if (std::find(categories.begin(), categories.end(), e.category) == categories.end())
            categories.push_back(e.category);
    for (const std::string& cat : categories) {
        bool any = false;
        for (const CoreClient::Event* e : hits) any = any || e->category == cat;
        if (!any) continue;

        ImGui::PushID(cat.c_str());
        if (searching) ImGui::SetNextItemOpen(true, ImGuiCond_Always);   // hits always visible
        if (ImGui::CollapsingHeader(cat.c_str())) {
            for (const CoreClient::Event* e : hits) {
                if (e->category != cat) continue;
                // A full-width selectable row with the name and (grey) map drawn over it.
                ImGui::PushID(e->id.c_str());
                const float x = ImGui::GetCursorPosX();
                if (ImGui::Selectable("##row")) picked = e->id;
                ImGui::SameLine(x);
                ImGui::TextUnformatted(e->name.c_str());
                if (!e->maps.empty()) {                                // all places of a rotating event
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", e->maps.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    return picked;
}

std::string RenderSuggestions(const std::vector<const CoreClient::Event*>& hits, int selected)
{
    std::string picked;
    for (int i = 0; i < (int)hits.size(); ++i) {
        const CoreClient::Event* e = hits[i];
        ImGui::PushID(e->id.c_str());
        const float x = ImGui::GetCursorPosX();
        if (ImGui::Selectable("##row", i == selected)) picked = e->id;
        ImGui::SameLine(x);
        ImGui::TextUnformatted(e->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", e->categoryShort.c_str());
        ImGui::PopID();
    }
    return picked;
}

}} // namespace Trailpack::EventPicker
