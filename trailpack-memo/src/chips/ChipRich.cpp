// ChipRich.cpp — rich-chip resolution + draw. See ChipRich.h.
//
// Degradation contract (must never crash, must always render):
//   * Trailpack Core absent / not ready yet   -> structural label, no icon, no
//     rich tooltip; context menu STILL works (it uses the raw stored code).
//   * Icon not yet downloaded / failed         -> text-only chip; still works.
//   * Record warm                              -> resolved name + icon + rich
//     tooltip (type-specific: item stats/flavour/value/flags + rune-set bonuses,
//     skill / trait desc+facts, waypoint map, build specialisations, recipe).
//
// Presence is read live every call via CoreClient — never latched.

#include "chips/ChipRich.h"

#include <windows.h>
#include <shellapi.h>

#include <cfloat>   // FLT_MAX
#include <cstdio>   // snprintf
#include <cstring>  // strlen
#include <string>

#include "chips/ChipResolver.h"            // Trailpack::ResolveChip (structural label)
#include "chips/ChipColor.h"               // Trailpack::ChipColor (native GW2 chip colours)
#include "integrations/CoreClient.h"       // Trailpack::CoreClient::Present / Resolve
#include "chips/IconCache.h"               // Trailpack::Icons::RequestUrl / Get
#include "chatlinks/ChatLinks.h"     // Split -> (linkType, id)
#include "core/I18n.h"                    // Tr
#include "ui/Theme.h"                   // PopupTextScope (popups ignore the note text colour)

namespace Trailpack {

namespace {

constexpr float kIconGap  = 3.0f;   // gap between the inline icon and the label

// Pull the (linkType, id) correlation key out of a chat code. A single chat
// code segments to one Link segment; if anything is off we return false and the
// caller falls back to a pure-structural chip (label only, no Core query).
bool LinkKey(const std::string& code, uint8_t& linkType, uint32_t& id)
{
    for (const auto& seg : ChatLinks::Split(code)) {
        if (seg.isLink) {
            linkType = (uint8_t)seg.type;
            id = seg.id;
            return true;
        }
    }
    return false;
}

// Resolve the chip to its display state for this frame.
// label: what to draw (bracketed). warm: true if a Core record was copied.
// iconUrl: from the record when warm (may be empty). color: structural tint.
struct ChipState {
    std::string   label;
    uint32_t      color = 0xFFA0A0A0;
    bool          warm  = false;
    TpcRecord     rec{};       // valid only when warm
};

ChipState Resolve(const std::string& code)
{
    ChipState st;
    // Structural label is the offline fallback (works without Trailpack Core).
    const ChipView structural = Trailpack::ResolveChip(code);
    st.label = structural.label.empty() ? "[link]" : structural.label;

    uint8_t linkType = 0; uint32_t id = 0;
    const bool haveKey = LinkKey(code, linkType, id);

    // Item rarity is only known from a warm Core record; default Unknown
    // (-> Basic colour) so cold/absent items still render in a sensible colour.
    uint8_t rarity = TPC_RARITY_UNKNOWN;

    if (haveKey && Trailpack::CoreClient::Present()) {
        // Ask for the rune / sigil records right away, so the tooltip has them when it opens.
        if (linkType == ChatLinks::LINK_ITEM) {
            ChatLinks::ItemLink di;
            if (ChatLinks::DecodeItem(code, di)) {
                Trailpack::CoreClient::Prefetch(di.upgrade1);
                Trailpack::CoreClient::Prefetch(di.upgrade2);
            }
        }
        TpcRecord rec{};
        if (Trailpack::CoreClient::Resolve(linkType, id, code.c_str(), rec) && rec.name[0] != '\0') {
            st.warm = true;
            st.rec  = rec;
            std::string name(rec.name);
            st.label = "[" + name + "]";   // bracket to match the structural style
            if (linkType == ChatLinks::LINK_ITEM) rarity = rec.rarity;
        }
    }

    // Colour is a pure function of (type, rarity): items by rarity, everything
    // else the native link blue — recomputed from the current record state.
    st.color = ChipColor(linkType, rarity);
    return st;
}

// Percent-encode everything that is not an RFC-3986 unreserved char. The chat
// code contains '[', '&', ']' which MUST be encoded for the wiki search URL.
std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

void OpenWiki(const std::string& code)
{
    const std::string url = "https://wiki.guildwars2.com/wiki/?search=" + UrlEncode(code);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Format a copper amount as "G g S s C c" (ASCII), dropping leading zero tiers.
void AppendCoin(std::string& out, int32_t copper)
{
    if (copper < 0) copper = 0;
    const int32_t g = copper / 10000;
    const int32_t s = (copper / 100) % 100;
    const int32_t c = copper % 100;
    char buf[64];
    if (g > 0)      snprintf(buf, sizeof buf, "%dg %ds %dc", g, s, c);
    else if (s > 0) snprintf(buf, sizeof buf, "%ds %dc", s, c);
    else            snprintf(buf, sizeof buf, "%dc", c);
    out += buf;
}

const char* BoundText(uint8_t bound)
{
    switch (bound) {
        case TPC_BOUND_ACCOUNT_ON_ACQUIRE: return Trailpack::I18n::Tr("Account Bound on Acquire");
        case TPC_BOUND_SOUL_ON_ACQUIRE:    return Trailpack::I18n::Tr("Soulbound on Acquire");
        case TPC_BOUND_ACCOUNT_ON_USE:     return Trailpack::I18n::Tr("Account Bound on Use");
        case TPC_BOUND_SOUL_ON_USE:        return Trailpack::I18n::Tr("Soulbound on Use");
        default:                  return nullptr;
    }
}

// Emit the pre-formatted fact lines (Core caps factCount, we clamp again).
void EmitFacts(const TpcRecord& rec)
{
    const int n = rec.factCount > TPC_MAX_FACTS ? TPC_MAX_FACTS : rec.factCount;
    for (int i = 0; i < n; ++i) {
        if (rec.facts[i].text[0]) ImGui::TextUnformatted(rec.facts[i].text);
    }
}

// Render a full item tooltip panel: rarity-coloured name + stat facts + vendor
// value / bind-trade flags + rune-set upgrade bonuses + flavour text. `code` is
// the item's chat code, used ONLY to decode its upgrade (rune/sigil) ids; pass
// "" when there is no chip code (e.g. a recipe's output item, which has an id
// but no pasted chat link), and the upgrade section is simply skipped.
void EmitItemPanel(const TpcRecord& rec, const std::string& code)
{
    const char* nm = rec.name[0] ? rec.name : Trailpack::I18n::Tr("(unnamed)");
    // Rarity-coloured header so the panel matches the chip colour.
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ChipColorForItemRarity(rec.rarity)),
                       "%s", nm);

    // Pre-formatted stat / meta lines (Defense, Weapon Strength, attributes,
    // infusion slots, slot, weight, required level).
    if (rec.factCount > 0) {
        ImGui::Separator();
        EmitFacts(rec);
    }
    // Vendor value + bind/trade flags. Core keeps these OUT of facts[], so
    // there is no duplication with the stat lines above.
    ImGui::Separator();
    std::string v = Trailpack::I18n::Tr("Vendor: ");
    AppendCoin(v, rec.vendorValue);
    ImGui::TextUnformatted(v.c_str());
    if (const char* b = BoundText(rec.bound)) ImGui::TextUnformatted(b);
    ImGui::TextUnformatted(rec.tradeable ? Trailpack::I18n::Tr("Tradeable on the Trading Post")
                                         : Trailpack::I18n::Tr("Not tradeable"));
    if (rec.noSell) ImGui::TextUnformatted(Trailpack::I18n::Tr("Cannot be sold to a vendor"));

    // Rune / sigil set bonuses live on the UPGRADE item's own record (Core
    // keeps records flat), so decode the chip's upgrade ids and resolve
    // each as a normal item link (prefetched when the chip was drawn). A
    // not-yet-warm upgrade is skipped and fills on a later frame.
    ChatLinks::ItemLink di;
    if (!code.empty() && ChatLinks::DecodeItem(code, di)) {
        const uint32_t ups[2] = { di.upgrade1, di.upgrade2 };
        bool header = false;
        for (uint32_t uid : ups) {
            if (uid == 0) continue;
            TpcRecord up{};
            if (Trailpack::CoreClient::Resolve(ChatLinks::LINK_ITEM, uid, nullptr, up)) {
                if (!header) {
                    ImGui::Separator();
                    ImGui::TextUnformatted(Trailpack::I18n::Tr("Upgrades:"));
                    header = true;
                }
                if (up.name[0]) ImGui::TextUnformatted(up.name);
                EmitFacts(up);
            }
        }
    }

    // Flavour text reads best at the very foot of the tooltip.
    if (rec.description[0]) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(rec.description);
        ImGui::PopTextWrapPos();
    }
}

// Description (wrapped) + facts, the body of skill / trait tooltips.
void EmitDescriptionAndFacts(const TpcRecord& rec)
{
    if (rec.description[0]) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(rec.description);
        ImGui::PopTextWrapPos();
    }
    if (rec.factCount > 0) {
        ImGui::Separator();
        EmitFacts(rec);
    }
}

// Build + emit the tooltip body for a warm record (type-specific). `code` is the
// chip's chat code, used only to fetch upgrade (rune/sigil) records for items.
void TooltipWarm(const TpcRecord& rec, const std::string& code)
{
    // Items get the full rarity-coloured equipment panel.
    if (rec.linkType == ChatLinks::LINK_ITEM) {
        EmitItemPanel(rec, code);
        return;
    }

    const char* nm = rec.name[0] ? rec.name : Trailpack::I18n::Tr("(unnamed)");
    ImGui::TextUnformatted(nm);

    switch (rec.linkType) {
        case ChatLinks::LINK_SKILL:
        case ChatLinks::LINK_TRAIT:
            EmitDescriptionAndFacts(rec);
            break;
        case ChatLinks::LINK_MAP: {   // waypoint / PoI
            if (rec.poiMapName[0]) {
                ImGui::Separator();
                std::string m = Trailpack::I18n::Tr("Map: ");
                m += rec.poiMapName;
                ImGui::TextUnformatted(m.c_str());
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", Trailpack::I18n::Tr("Left-click: copy the waypoint code."));
            break;
        }
        case ChatLinks::LINK_BUILD:   // the specialisation lines
            if (rec.factCount > 0) {
                ImGui::Separator();
                EmitFacts(rec);
            }
            break;
        case ChatLinks::LINK_RECIPE: {
            // name[] above reads "Recipe: <output>"; facts[] = one line per ingredient,
            // then "Required Rating: N".
            if (rec.factCount > 0) {
                ImGui::Separator();
                EmitFacts(rec);
            }
            // Second panel: the output item's full equipment tooltip, like the game's
            // two-panel recipe view. A not-yet-warm output fills on a later frame.
            if (rec.recipeOutputItemId != 0) {
                TpcRecord out{};
                if (Trailpack::CoreClient::Resolve(ChatLinks::LINK_ITEM, rec.recipeOutputItemId, nullptr, out)
                    && out.name[0]) {
                    ImGui::Separator();
                    EmitItemPanel(out, std::string());  // no chip code -> no upgrade decode
                }
            }
            break;
        }
        default:
            if (rec.description[0]) EmitDescriptionAndFacts(rec);   // skin and others
            break;
    }
}

} // namespace

std::string ChipLabel(const std::string& code)
{
    return Resolve(code).label;
}

uint32_t ChipDisplayColor(const std::string& code)
{
    return Resolve(code).color;
}

// Width = optional inline icon (square fontSize) + gap + coloured label text.
// No box/padding — the chip is just coloured inline text (matching the chatbox).
// Re-derives the same ChipState as the draw so the two agree.
static float ComputeWidth(ImFont* font, float fontSize, const ChipState& st, bool withIcon)
{
    float w = 0.0f;
    // Icon is shown only when warm AND the texture is ready; the wrap pre-check
    // may briefly disagree with the draw on the single frame an icon finishes
    // loading — harmless (only affects a wrap decision, never correctness).
    if (withIcon && st.warm && st.rec.iconUrl[0]) {
        Trailpack::Icons::RequestUrl(st.rec.iconUrl);
        if (Trailpack::Icons::Get(st.rec.iconUrl) != nullptr) w += fontSize + kIconGap;
    }
    w += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, st.label.c_str()).x;
    return w;
}

float MeasureRichChip(ImFont* font, float fontSize, const std::string& code,
                      const ChipDoneStyle* done)
{
    return ComputeWidth(font, fontSize, Resolve(code), done == nullptr);
}

float DrawRichChip(ImDrawList* dl, ImFont* font, float fontSize, const ImVec2& pos,
                   const std::string& code, int uid, const ChipDoneStyle* done)
{
    const ChipState st = Resolve(code);
    const float width = ComputeWidth(font, fontSize, st, done == nullptr);

    // No box / border / background — the chip is coloured inline text. The rect
    // below is used only for hover/right-click hit-testing, never drawn.
    const float padY = 1.0f;
    const ImVec2 rMin(pos.x, pos.y - padY);
    const ImVec2 rMax(pos.x + width, pos.y + fontSize + padY);

    float penX = pos.x;

    // Optional inline icon (square, fontSize x fontSize) — only when warm + ready.
    if (done == nullptr && st.warm && st.rec.iconUrl[0]) {
        Texture_t* tex = Trailpack::Icons::Get(st.rec.iconUrl);  // never null Resource
        if (tex != nullptr) {
            const ImVec2 iMin(penX, pos.y);
            const ImVec2 iMax(penX + fontSize, pos.y + fontSize);
            dl->AddImage((ImTextureID)tex->Resource, iMin, iMax);
            penX += fontSize + kIconGap;
        }
    }

    // Label as coloured inline text — colour conveys type (and item rarity).
    dl->AddText(font, fontSize, ImVec2(penX, pos.y), done != nullptr ? done->textCol : st.color,
                st.label.c_str());
    if (done != nullptr) return width;   // checked task: plain text (the options are events-only)

    // ── Interaction: manual hit-test against the chip rect ──────────────────
    // We are inside the view child window's draw, so OpenPopup/BeginPopup hash
    // their id against THIS window (matches where the body is drawn). BeginPopup
    // runs every frame (not only when hovered) so an open popup keeps showing.
    char pid[32];
    snprintf(pid, sizeof pid, "chip_ctx_%d", uid);

    // A waypoint chip copies its code on left-click (paste it in the game chat to travel).
    uint8_t linkType = 0; uint32_t linkId = 0;
    const bool waypoint = LinkKey(code, linkType, linkId) && linkType == ChatLinks::LINK_MAP;

    const bool hovering = ImGui::IsMouseHoveringRect(rMin, rMax);
    if (hovering) {
        // Tooltip (suppress while the context menu is open to avoid overlap).
        if (!ImGui::IsPopupOpen(pid)) {
            Theme::PopupTextScope popupText;
            ImGui::BeginTooltip();
            if (st.warm) {
                TooltipWarm(st.rec, code);
            } else {
                ImGui::TextUnformatted(st.label.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(Trailpack::CoreClient::Present()
                                           ? Trailpack::I18n::Tr("Loading details...")
                                           : Trailpack::I18n::Tr("Names, icons and tooltips come with the Trailpack Core addon."));
                ImGui::TextUnformatted(waypoint ? Trailpack::I18n::Tr("Left-click: copy the waypoint code.")
                                                : Trailpack::I18n::Tr("Right-click to copy the code or open the wiki."));
            }
            ImGui::EndTooltip();
        }
        if (waypoint) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (waypoint && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ImGui::SetClipboardText(code.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(pid);
        }
    }

    // Context menu — both actions use the raw stored code, so they work without
    // Trailpack Core. Begun every frame in this window scope.
    Theme::PopupTextScope popupText;
    if (ImGui::BeginPopup(pid)) {
        if (ImGui::MenuItem(Trailpack::I18n::Tr("Copy chat code"))) ImGui::SetClipboardText(code.c_str());
        if (ImGui::MenuItem(Trailpack::I18n::Tr("Open in wiki")))   OpenWiki(code);
        ImGui::EndPopup();
    }

    return width;
}

} // namespace Trailpack
