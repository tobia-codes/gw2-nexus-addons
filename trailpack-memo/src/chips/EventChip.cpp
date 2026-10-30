// EventChip.cpp — see EventChip.h.
#include "chips/EventChip.h"

#include <windows.h>
#include <shellapi.h>

#include <cfloat>   // FLT_MAX
#include <cstdio>
#include <ctime>
#include <string>
#include "events/Events.h"
#include "core/I18n.h"
#include "integrations/CoreClient.h"
#include "ui/Theme.h"

namespace Trailpack {

using I18n::Tr;

namespace {

// "Waypoint copied" hint: shown at the mouse for a short while after a left click.
constexpr double kCopiedSeconds = 1.5;
double s_CopiedUntil = 0.0;
int    s_CopiedUid   = -1;   // the chip that was clicked (stable per note while it is shown)

using CoreClient::Event;

// The running occurrence, else the next one (zeroed if Core can't say).
CoreClient::Occurrence StateOf(const Event& e, int64_t now)
{
    CoreClient::Occurrence st;
    CoreClient::EventState(e.id, now, st);
    return st;
}

ImU32 StateColor(const Event* e, int64_t now)
{
    if (e == nullptr) return ImGui::GetColorU32(ImGuiCol_TextDisabled);
    return ImGui::GetColorU32(StateOf(*e, now).active ? Theme::EVENT_ACTIVE : Theme::EVENT_UPCOMING);
}

// Unknown id: "Unknown event (<id>)"; without Trailpack Core the name is read off the id.
std::string UnknownLabel(const std::string& id)
{
    if (!CoreClient::Present()) return Events::PrettyId(id);
    char buf[128];
    snprintf(buf, sizeof buf, Tr("Unknown event (%s)"), id.c_str());
    return buf;
}

// "<name> - in 23 Min" / "<name> - seit 4 Min"; unknown ids: "Unknown event (<id>)".
// A checked task without the time option shows the name only.
std::string ViewLabel(const Event* e, const std::string& id, int64_t now,
                      const ChipDoneStyle* done = nullptr)
{
    if (e == nullptr) return UnknownLabel(id);
    if (done != nullptr && !done->showTime) return e->name;
    const CoreClient::Occurrence st = StateOf(*e, now);
    const int64_t secs = st.active ? now - st.start : st.start - now;
    return e->name + " - " + Events::FormatTime(secs, st.active);
}

// Local clock time "HH:MM" of a UTC instant. Converted per instant, so each start gets the
// UTC offset (incl. daylight saving) that applies on its own date.
std::string LocalClock(int64_t t)
{
    const std::time_t tt = (std::time_t)t;
    std::tm local = {};
    localtime_s(&local, &tt);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &local);
    return buf;
}

std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += '_';   // wiki page titles use underscores
        } else if (c == '#') {
            out += '#';   // keep the section anchor
        } else {
            out += '%'; out += hex[c >> 4]; out += hex[c & 15];
        }
    }
    return out;
}

void OpenWiki(const Event& e)
{
    const std::string url = "https://wiki.guildwars2.com/wiki/" + UrlEncode(e.wikiPage);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// The place that counts now: the running occurrence's, else the next one's (a rotating event
// moves on only after its current run has ended).
const CoreClient::EventPlace& CurrentPlace(const Event& e, int64_t now)
{
    const uint8_t p = StateOf(e, now).place;
    return e.places[p < e.places.size() ? p : 0];
}

void CopyWaypoint(const Event& e, int64_t now, int uid)
{
    const std::string& wp = CurrentPlace(e, now).waypoint;
    if (wp.empty()) return;
    ImGui::SetClipboardText(wp.c_str());
    s_CopiedUntil = ImGui::GetTime() + kCopiedSeconds;
    s_CopiedUid   = uid;
}

void Tooltip(const Event& e, int64_t now, bool clickCopy)
{
    ImGui::TextUnformatted(e.name.c_str());
    ImGui::TextDisabled("%s", e.category.c_str());
    ImGui::Separator();

    const CoreClient::Occurrence st = StateOf(e, now);
    if (st.active) {
        ImGui::Text(Tr("Running since %s, ends at %s"), LocalClock(st.start).c_str(),
                    LocalClock(st.end).c_str());
    }
    // Rotating events name the map of every start: "03:20 (Iron Marches)".
    std::string starts;
    for (const CoreClient::Occurrence& o : CoreClient::EventNext(e.id, now, 3)) {
        if (!starts.empty()) starts += ", ";
        starts += LocalClock(o.start);
        if (e.places.size() > 1 && o.place < e.places.size()) starts += " (" + e.places[o.place].map + ")";
    }
    ImGui::Text(Tr("Next starts: %s"), starts.c_str());
    ImGui::Text(Tr("Duration: %s"), Events::FormatDuration(st.end - st.start).c_str());

    // Map and waypoint of the running occurrence, else of the next one.
    const CoreClient::EventPlace& place = e.places[st.place < e.places.size() ? st.place : 0];
    if (!place.map.empty()) ImGui::Text(Tr("Map: %s"), place.map.c_str());
    if (!place.waypoint.empty()) {
        const std::string& wp = place.waypointName.empty() ? place.waypoint : place.waypointName;
        ImGui::Text(Tr("Waypoint: %s"), wp.c_str());
        if (clickCopy) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", Tr("Left-click: copy waypoint. Right-click: more."));
        }
    } else {
        ImGui::TextDisabled("%s", Tr("No waypoint known for this event."));
    }
}

} // namespace

std::string EventChipLabel(const std::string& token)
{
    std::string id;
    Events::IsToken(token, &id);
    const Event* e = CoreClient::FindEvent(id);
    return e != nullptr ? e->name : UnknownLabel(id);
}

ImU32 EventChipColor(const std::string& token)
{
    std::string id;
    Events::IsToken(token, &id);
    return CoreClient::FindEvent(id) != nullptr ? ImGui::GetColorU32(Theme::EVENT_UPCOMING)
                                                : ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

float MeasureEventChip(ImFont* font, float fontSize, const std::string& id,
                       const ChipDoneStyle* done)
{
    const std::string label = ViewLabel(CoreClient::FindEvent(id), id, (int64_t)std::time(nullptr), done);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str()).x;
}

float DrawEventChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                    const std::string& id, int uid, const ChipDoneStyle* done)
{
    const int64_t now = (int64_t)std::time(nullptr);
    const Event* e = CoreClient::FindEvent(id);
    const std::string label = ViewLabel(e, id, now, done);
    const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str()).x;
    ImU32 col = StateColor(e, now);
    if (done != nullptr) {
        // Countdown kept: the event colour much lighter, clearly apart from open tasks.
        const ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
        const float  k = 0.6f;
        col = done->showTime ? ImGui::GetColorU32(ImVec4(c.x + (1.0f - c.x) * k, c.y + (1.0f - c.y) * k,
                                                         c.z + (1.0f - c.z) * k, c.w))
                             : done->textCol;
    }
    dl->AddText(font, fontSize, pos, col, label.c_str());
    if (e == nullptr || (done != nullptr && !done->popups)) return width;
    const bool clickCopy = done == nullptr;

    // ── Interaction (hit-test only, like rich chips; popups in this window's id scope) ──
    char pid[32];
    snprintf(pid, sizeof pid, "event_ctx_%d", uid);
    const ImVec2 rMin(pos.x, pos.y - 1.0f), rMax(pos.x + width, pos.y + fontSize + 1.0f);
    const bool hovering = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rMin, rMax);
    {
        // Tooltips and the menu use the popup text colour, not the note text colour.
        Theme::PopupTextScope popupText;
        const bool copiedHint = uid == s_CopiedUid && ImGui::GetTime() < s_CopiedUntil;
        if (hovering) {
            if (clickCopy && !CurrentPlace(*e, now).waypoint.empty()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clickCopy && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) CopyWaypoint(*e, now, uid);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(pid);
            if (!copiedHint && !ImGui::IsPopupOpen(pid)) {
                ImGui::BeginTooltip();
                Tooltip(*e, now, clickCopy);
                ImGui::EndTooltip();
            }
        }
        // The hint follows the mouse for its full time, also after leaving the chip.
        if (copiedHint) ImGui::SetTooltip("%s", Tr("Waypoint copied"));
        if (ImGui::BeginPopup(pid)) {
            if (ImGui::MenuItem(Tr("Copy waypoint"), nullptr, false, !CurrentPlace(*e, now).waypoint.empty()))
                CopyWaypoint(*e, now, uid);
            if (ImGui::MenuItem(Tr("Open in wiki"))) OpenWiki(*e);
            ImGui::EndPopup();
        }
    }
    return width;
}

} // namespace Trailpack
