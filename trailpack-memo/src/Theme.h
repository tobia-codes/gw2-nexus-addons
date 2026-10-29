#pragma once
// Theme.h — Trailpack Memo accent palette (inherited from Magpie Memo) + RAII push/pop helpers.
// Inherits Nexus's default ImGui style; applies the PieOrCake suite's
// consistent accent colours per-widget (no global ApplyTheme call needed).

#include "imgui.h"

namespace Theme {

// ── Gold / amber window border ────────────────────────────────────────────
// ImVec4 lacks a constexpr constructor in this ImGui version; use inline const.
inline const ImVec4 BORDER_GOLD        = { 0.90f, 0.75f, 0.25f, 0.90f };
constexpr float     BORDER_SIZE        = 2.0f;

// ── Green action-button set ───────────────────────────────────────────────
inline const ImVec4 BTN_GREEN          = { 0.18f, 0.45f, 0.18f, 0.90f };
inline const ImVec4 BTN_GREEN_HOVERED  = { 0.25f, 0.60f, 0.25f, 0.95f };
inline const ImVec4 BTN_GREEN_ACTIVE   = { 0.30f, 0.70f, 0.30f, 1.00f };

// ── Amber secondary-button set ────────────────────────────────────────────
inline const ImVec4 BTN_AMBER          = { 0.22f, 0.20f, 0.12f, 0.80f };
inline const ImVec4 BTN_AMBER_HOVERED  = { 0.35f, 0.30f, 0.14f, 0.90f };
inline const ImVec4 BTN_AMBER_ACTIVE   = { 0.45f, 0.38f, 0.16f, 1.00f };

// ── Dark frame background ─────────────────────────────────────────────────
inline const ImVec4 FRAME_BG_DARK      = { 0.10f, 0.10f, 0.13f, 1.00f };

// ── Edit-mode frame background (warmer, slightly blue-shifted) ────────────
// Visually distinct from FRAME_BG_DARK so the user knows they are editing.
inline const ImVec4 EDIT_BG            = { 0.08f, 0.11f, 0.20f, 1.00f };

// ── Daily / weekly note badge defaults ────────────────────────────────────
inline const ImVec4 BADGE_DAILY        = { 0.90f, 0.62f, 0.20f, 1.00f };   // amber
inline const ImVec4 BADGE_WEEKLY       = { 0.40f, 0.60f, 0.95f, 1.00f };   // blue
inline const ImVec4 BADGE_TEXT         = { 0.08f, 0.08f, 0.08f, 1.00f };

// ── User colours (Options pane, persisted in settings.json) ───────────────
// Border defaults to BORDER_GOLD. Background and text default to Nexus's own style;
// they only override it once the user picks a colour (has* = true).
struct UserColors {
    bool   showBorder    = false;  // off: no window border (colour still used elsewhere)
    ImVec4 border        = BORDER_GOLD;
    bool   hasBackground = false;
    ImVec4 background    = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool   hasText       = false;
    ImVec4 text          = { 0.0f, 0.0f, 0.0f, 0.0f };
    // Group names in the note list: default follows `border`; a group can override it.
    bool   hasGroupName  = false;
    ImVec4 groupName     = { 0.0f, 0.0f, 0.0f, 0.0f };
    // "No group" label in the note list: default is Nexus's disabled-text colour.
    bool   hasNoGroup    = false;
    ImVec4 noGroup       = { 0.0f, 0.0f, 0.0f, 0.0f };
    // Daily / weekly badge in the note list (the badge colour also tints the countdown).
    ImVec4 dailyBadge      = BADGE_DAILY;
    ImVec4 dailyBadgeText  = BADGE_TEXT;
    ImVec4 weeklyBadge     = BADGE_WEEKLY;
    ImVec4 weeklyBadgeText = BADGE_TEXT;
    // Note list and note pane, each unset = as before (Nexus child background, window text).
    // A picked background is drawn exactly: nested panes inside the note are transparent.
    bool   hasListBg     = false;
    ImVec4 listBg        = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool   hasListText   = false;
    ImVec4 listText      = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool   hasNoteBg     = false;
    ImVec4 noteBg        = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool   hasNoteText   = false;
    ImVec4 noteText      = { 0.0f, 0.0f, 0.0f, 0.0f };
    // Pane borders (list / note): off by default; colour follows `border` until picked.
    bool   showListBorder = false;
    bool   hasListBorder  = false;
    ImVec4 listBorder     = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool   showNoteBorder = false;
    bool   hasNoteBorder  = false;
    ImVec4 noteBorder     = { 0.0f, 0.0f, 0.0f, 0.0f };
};
UserColors& Colors();   // the live, mutable set (Theme.cpp)

// Window text colour (user-set, else Nexus's), captured by PushWindowStyle. Tooltips and
// popups opened inside the note pane push it back, so they ignore the note text colour.
ImVec4 WindowText();

// Border of a pane (child window): size and colour are read by BeginChild, so push right
// before it and pop right after (later ImGuiCol_Border users inside keep the window colour).
void PushPaneBorder(bool show, bool hasColor, const ImVec4& color);
void PopPaneBorder();
inline void PushListBorder() { const UserColors& c = Colors(); PushPaneBorder(c.showListBorder, c.hasListBorder, c.listBorder); }
inline void PushNoteBorder() { const UserColors& c = Colors(); PushPaneBorder(c.showNoteBorder, c.hasNoteBorder, c.noteBorder); }

// Background of an input field on a pane background `bg`: slightly darker on a light
// pane, slightly lighter on a dark one, opaque (the editor on a custom note background).
ImVec4 FieldBg(const ImVec4& bg);

// Push ImGuiCol_Text for a scope when `set` (e.g. note text); no-op otherwise.
struct TextColorScope {
    bool pushed;
    TextColorScope(bool set, const ImVec4& col) : pushed(set) { if (pushed) ImGui::PushStyleColor(ImGuiCol_Text, col); }
    ~TextColorScope() { if (pushed) ImGui::PopStyleColor(); }
    TextColorScope(const TextColorScope&) = delete;
    TextColorScope& operator=(const TextColorScope&) = delete;
};

// ── Scoped helpers ────────────────────────────────────────────────────────
// Push the window border + user colours (border, background, text) for the whole
// window, its child panes and its modals. Call PopWindowStyle() after ImGui::End().
void PushWindowStyle();
void PopWindowStyle();

// Push green action-button colours. Call PopButton() after the button widget.
inline void PushGreenButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_GREEN);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_GREEN_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_GREEN_ACTIVE);
}
inline void PopButton() {
    ImGui::PopStyleColor(3);
}

// Push amber secondary-button colours. Call PopButton() after the button widget.
inline void PushAmberButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,        BTN_AMBER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BTN_AMBER_HOVERED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BTN_AMBER_ACTIVE);
}

// Push dark frame background (e.g. for text input fields).
// Call PopFrameBg() after the widget.
inline void PushDarkFrameBg() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, FRAME_BG_DARK);
}
// Push edit-mode frame background (blue-tinted dark). Call PopFrameBg() after.
inline void PushEditBg() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, EDIT_BG);
}
inline void PopFrameBg() {
    ImGui::PopStyleColor(1);
}

} // namespace Theme
