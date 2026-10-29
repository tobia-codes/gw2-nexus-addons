// EventChip.cpp — see EventChip.h.
#include "EventChip.h"

#include <windows.h>
#include <shellapi.h>

#include <cfloat>   // FLT_MAX
#include <cstdio>
#include <ctime>
#include <string>
#include "Events.h"
#include "I18n.h"
#include "Theme.h"

namespace Magpie {

using I18n::Tr;

namespace {

// "Waypoint copied" hint: shown at the mouse for a short while after a left click.
constexpr double kCopiedSeconds = 1.5;
double s_CopiedUntil = 0.0;
int    s_CopiedUid   = -1;   // the chip that was clicked (stable per note while it is shown)

ImU32 StateColor(const Events::EventDef* e, int64_t now)
{
    if (e == nullptr) return ImGui::GetColorU32(ImGuiCol_TextDisabled);
    return ImGui::GetColorU32(Events::State(*e, now).active ? Theme::EVENT_ACTIVE
                                                            : Theme::EVENT_UPCOMING);
}

std::string UnknownLabel(const std::string& id)
{
    char buf[128];
    snprintf(buf, sizeof buf, Tr("Unknown event (%s)"), id.c_str());
    return buf;
}

// "<name> - in 23 Min" / "<name> - seit 4 Min"; unknown ids: "Unknown event (<id>)".
// A checked task without the time option shows the name only.
std::string ViewLabel(const Events::EventDef* e, const std::string& id, int64_t now,
                      const ChipDoneStyle* done = nullptr)
{
    if (e == nullptr) return UnknownLabel(id);
    if (done != nullptr && !done->showTime) return Events::Name(*e, I18n::Current());
    const Events::Status st = Events::State(*e, now);
    const int64_t secs = st.active ? now - st.start : st.start - now;
    return std::string(Events::Name(*e, I18n::Current())) + " - " + Events::FormatTime(secs, st.active);
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

void OpenWiki(const Events::EventDef& e)
{
    const std::string url = "https://wiki.guildwars2.com/wiki/" + UrlEncode(e.wikiPage);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// The place that counts now: the running occurrence's, else the next one's (a rotating event
// moves on only after its current run has ended).
const Events::PlaceDef& CurrentPlace(const Events::EventDef& e, int64_t now)
{
    return e.places[Events::State(e, now).place];
}

void CopyWaypoint(const Events::EventDef& e, int64_t now, int uid)
{
    const char* wp = CurrentPlace(e, now).waypoint;
    if (wp == nullptr) return;
    ImGui::SetClipboardText(wp);
    s_CopiedUntil = ImGui::GetTime() + kCopiedSeconds;
    s_CopiedUid   = uid;
}

void Tooltip(const Events::EventDef& e, int64_t now, bool clickCopy)
{
    const I18n::Lang lang = I18n::Current();
    ImGui::TextUnformatted(Events::Name(e, lang));
    ImGui::TextDisabled("%s", Events::CategoryName(e.category, lang));
    ImGui::Separator();

    const Events::Status st = Events::State(e, now);
    if (st.active) {
        ImGui::Text(Tr("Running since %s, ends at %s"), LocalClock(st.start).c_str(),
                    LocalClock(st.end).c_str());
    }
    // Rotating events name the map of every start: "03:20 (Iron Marches)".
    std::string starts;
    for (const Events::Occurrence& o : Events::NextOccurrences(e, now, 3)) {
        if (!starts.empty()) starts += ", ";
        starts += LocalClock(o.start);
        if (e.placeCount > 1) starts += std::string(" (") + Events::MapName(e.places[o.place], lang) + ")";
    }
    ImGui::Text(Tr("Next starts: %s"), starts.c_str());
    ImGui::Text(Tr("Duration: %s"), Events::FormatDuration(st.end - st.start).c_str());

    // Map and waypoint of the running occurrence, else of the next one.
    const Events::PlaceDef& place = e.places[st.place];
    const char* map = Events::MapName(place, lang);
    if (map[0] != '\0') ImGui::Text(Tr("Map: %s"), map);
    if (place.waypoint != nullptr) {
        const char* wp = Events::WaypointName(place, lang);
        ImGui::Text(Tr("Waypoint: %s"), wp[0] != '\0' ? wp : place.waypoint);
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
    const Events::EventDef* e = Events::Find(id);
    return e != nullptr ? Events::Name(*e, I18n::Current()) : UnknownLabel(id);
}

ImU32 EventChipColor(const std::string& token)
{
    std::string id;
    Events::IsToken(token, &id);
    return Events::Find(id) != nullptr ? ImGui::GetColorU32(Theme::EVENT_UPCOMING)
                                       : ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

float MeasureEventChip(ImFont* font, float fontSize, const std::string& id,
                       const ChipDoneStyle* done)
{
    const std::string label = ViewLabel(Events::Find(id), id, (int64_t)std::time(nullptr), done);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str()).x;
}

float DrawEventChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                    const std::string& id, int uid, const ChipDoneStyle* done)
{
    const int64_t now = (int64_t)std::time(nullptr);
    const Events::EventDef* e = Events::Find(id);
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
            if (clickCopy && CurrentPlace(*e, now).waypoint != nullptr) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
            if (ImGui::MenuItem(Tr("Copy waypoint"), nullptr, false, CurrentPlace(*e, now).waypoint != nullptr))
                CopyWaypoint(*e, now, uid);
            if (ImGui::MenuItem(Tr("Open in wiki"))) OpenWiki(*e);
            ImGui::EndPopup();
        }
    }
    return width;
}

} // namespace Magpie
