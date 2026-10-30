#pragma once
// ============================================================================
// Style — the look shared by every Trailpack addon (ImGui 1.80, as shipped by
// Nexus): accent palette, button colours, corner rounding, plain settings
// windows, help markers and greyed-out ("locked") items.
//
// Addon-specific colours (e.g. Memo's user themes) stay in the addon; this is
// only what all of them have in common, so their settings pages look alike.
// ============================================================================

#include "imgui.h"

namespace Trailpack { namespace Ui {

// ── Palette ──────────────────────────────────────────────────────────────
// ImVec4 lacks a constexpr constructor in this ImGui version; use inline const.
inline const ImVec4 BORDER_GOLD       = { 0.90f, 0.75f, 0.25f, 0.90f };
constexpr float     BORDER_SIZE       = 2.0f;

// Green action buttons. Opaque: nothing behind them may show through.
inline const ImVec4 BTN_GREEN         = { 0.18f, 0.45f, 0.18f, 1.00f };
inline const ImVec4 BTN_GREEN_HOVERED = { 0.25f, 0.60f, 0.25f, 1.00f };
inline const ImVec4 BTN_GREEN_ACTIVE  = { 0.30f, 0.70f, 0.30f, 1.00f };

// Amber secondary buttons.
inline const ImVec4 BTN_AMBER         = { 0.22f, 0.20f, 0.12f, 1.00f };
inline const ImVec4 BTN_AMBER_HOVERED = { 0.35f, 0.30f, 0.14f, 1.00f };
inline const ImVec4 BTN_AMBER_ACTIVE  = { 0.45f, 0.38f, 0.16f, 1.00f };

// Dark frame background, and the blue-shifted one of edit mode.
inline const ImVec4 FRAME_BG_DARK     = { 0.10f, 0.10f, 0.13f, 1.00f };
inline const ImVec4 EDIT_BG           = { 0.08f, 0.11f, 0.20f, 1.00f };

// Status lines on settings pages ("connected", "error").
inline const ImVec4 STATUS_OK         = { 0.50f, 0.85f, 0.50f, 1.00f };
inline const ImVec4 STATUS_WARN       = { 0.95f, 0.70f, 0.30f, 1.00f };
inline const ImVec4 STATUS_ERROR      = { 0.95f, 0.45f, 0.40f, 1.00f };

// ── Buttons / frames ─────────────────────────────────────────────────────
inline void PushGreenButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_GREEN);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_GREEN_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_GREEN_ACTIVE);
}
inline void PushAmberButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_AMBER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_AMBER_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_AMBER_ACTIVE);
}
inline void PopButton() { ImGui::PopStyleColor(3); }

inline void PushDarkFrameBg() { ImGui::PushStyleColor(ImGuiCol_FrameBg, FRAME_BG_DARK); }
inline void PushEditBg()      { ImGui::PushStyleColor(ImGuiCol_FrameBg, EDIT_BG); }
inline void PopFrameBg()      { ImGui::PopStyleColor(1); }

// ── Corner rounding ──────────────────────────────────────────────────────
// Rounding in px for the addon's windows (windows, panes, buttons, fields, popups);
// per DLL, set from the addon's settings (default 0 = square).
void  SetRounding(float px);
float Rounding();
// Push / pop Rounding() for every rounded ImGui element.
void  PushRounding();
void  PopRounding();

// ── Windows ──────────────────────────────────────────────────────────────
// ImGui::Begin with a taller title bar (more room around the close button). Always pair with
// ImGui::End, like Begin.
bool  BeginWindow(const char* name, bool* open);
// FramePadding.y of that title bar (for items drawn into it, e.g. a "?" button).
float TitleBarPadY();

// Plain-styled settings / guide windows: Nexus's colours, but opaque window and popup
// backgrounds and the corner rounding. Create before ImGui::Begin, let it end after ImGui::End.
struct PlainWindowScope {
    PlainWindowScope();
    ~PlainWindowScope();
    PlainWindowScope(const PlainWindowScope&) = delete;
    PlainWindowScope& operator=(const PlainWindowScope&) = delete;
};

// ── Settings page helpers ────────────────────────────────────────────────
// A grey "(?)" on the same line; hovering shows `text`.
void HelpMarker(const char* text);

// Greyed-out items for a feature that is locked (e.g. needs Trailpack Core): items between
// construction and destruction ignore input and are drawn at half alpha when `locked`;
// no-op otherwise. ImGui 1.80 has no BeginDisabled -- this is its stand-in.
struct LockedScope {
    explicit LockedScope(bool locked);
    ~LockedScope();
    LockedScope(const LockedScope&) = delete;
    LockedScope& operator=(const LockedScope&) = delete;
private:
    bool locked_;
};

// Tooltip for the last item that also works when the item is locked (disabled).
// Call right after the item.
void LockedHint(const char* text);

}} // namespace Trailpack::Ui
