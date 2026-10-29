// dllmain.cpp — Trailpack Memo Nexus addon lifecycle.
// Fork of PieOrCake's Magpie Memo (MIT) with task-list checkboxes and a markdown toolbar.
// Stage 2: two-pane notes shell + JSON persistence.

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>

#include "nexus/Nexus.h"
#include "imgui.h"
#include "Theme.h"
#include "NotesWindow.h"
#include "DecoderClient.h"
#include "PieUiClient.h"
#include "I18n.h"
#include "FontScale.h"
#include "SafeFile.h"
#include "Settings.h"
#include "HelpWindow.h"
#include "NotesUi.h"
#include "IconCache.h"
#include "EmbeddedIcon.h"   // MAGPIE_ICON[] + MAGPIE_ICON_size (generated from icon.png)

// ── Quick-access identifiers ──────────────────────────────────────────────
#define QA_ID    "QA_TRAILPACK_MEMO"
#define TEX_ICON "TEX_TRAILPACK_MEMO_ICON"
#define KB_TOGGLE "KB_TRAILPACK_MEMO_TOGGLE"
#define LOC_LANG_PROBE "TPM_LANG_PROBE"   // Nexus localization key whose value is the active language
#define WINDOW_NAME "Trailpack Memo"

// ── Version constants ─────────────────────────────────────────────────────
#define V_MAJOR    1
#define V_MINOR    0
#define V_BUILD    0
#define V_REVISION 0

// ── Globals ───────────────────────────────────────────────────────────────
AddonAPI_t*      APIDefs         = nullptr;
bool             g_WindowVisible = false;
bool             g_QAIconAdded   = false;  // tracks whether the shortcut is currently registered
bool             g_HelpVisible   = false;  // the guide window ("?" in the title bar)
bool             g_SettingsVisible = false;  // own settings window (menu -> "Settings...")
Magpie::Settings g_Settings;               // settings.json (see Settings.h); colours, preview
                                           // and list width live in Theme / g_Notes meanwhile
NotesWindow      g_Notes;

// ── Forward declarations ──────────────────────────────────────────────────
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();
static void RenderOptions();

using Magpie::I18n::Tr;

// Language Nexus is currently set to, read through a probe key registered at load
// (Localization_Set for "en" and "de"). Any other Nexus language returns the key
// itself -> English.
static Magpie::I18n::Lang NexusLang() {
    if (!APIDefs || !APIDefs->Localization_Translate) return Magpie::I18n::Lang::En;
    const char* code = APIDefs->Localization_Translate(LOC_LANG_PROBE);
    return (code && strcmp(code, "de") == 0) ? Magpie::I18n::Lang::De : Magpie::I18n::Lang::En;
}

// Resolve the setting to the language used this frame (cheap; called per render).
static void ApplyLanguage() {
    using Magpie::I18n::Lang;
    switch (g_Settings.language) {
        case Magpie::Settings::Language::En:   Magpie::I18n::SetLang(Lang::En); break;
        case Magpie::Settings::Language::De:   Magpie::I18n::SetLang(Lang::De); break;
        case Magpie::Settings::Language::Auto: Magpie::I18n::SetLang(NexusLang()); break;
    }
}

// ── Settings persistence (settings.json in the addon data dir) ────────────
// g_Settings is the file's model; the live colours (Theme::Colors), the preview toggle and
// the list width (g_Notes) are copied in on load and collected again before saving.

static ImVec4 ToImVec4(const Magpie::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }
static Magpie::Rgba ToRgba(const ImVec4& c)   { return Magpie::Rgba{ c.x, c.y, c.z, c.w }; }

static std::string SettingsPath() {
    if (!APIDefs) return std::string();
    const char* dir = APIDefs->Paths_GetAddonDirectory("TrailpackMemo");
    if (!dir || !*dir) return std::string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (std::filesystem::path(dir) / "settings.json").string();
}

static void ApplySettings() {
    Theme::UserColors& c = Theme::Colors();
    c.showBorder    = g_Settings.showBorder;
    c.border        = ToImVec4(g_Settings.border);
    c.hasBackground = g_Settings.background.set;
    c.background    = ToImVec4(g_Settings.background.color);
    c.hasText       = g_Settings.text.set;
    c.text          = ToImVec4(g_Settings.text.color);
    c.hasGroupName  = g_Settings.groupName.set;
    c.groupName     = ToImVec4(g_Settings.groupName.color);
    c.hasNoGroup    = g_Settings.noGroup.set;
    c.noGroup       = ToImVec4(g_Settings.noGroup.color);
    c.hasListBg     = g_Settings.listBackground.set;
    c.listBg        = ToImVec4(g_Settings.listBackground.color);
    c.hasListText   = g_Settings.listText.set;
    c.listText      = ToImVec4(g_Settings.listText.color);
    c.hasNoteBg     = g_Settings.noteBackground.set;
    c.noteBg        = ToImVec4(g_Settings.noteBackground.color);
    c.hasNoteText   = g_Settings.noteText.set;
    c.noteText      = ToImVec4(g_Settings.noteText.color);
    c.showListBorder = g_Settings.showListBorder;
    c.hasListBorder  = g_Settings.listBorder.set;
    c.listBorder     = ToImVec4(g_Settings.listBorder.color);
    c.showNoteBorder = g_Settings.showNoteBorder;
    c.hasNoteBorder  = g_Settings.noteBorder.set;
    c.noteBorder     = ToImVec4(g_Settings.noteBorder.color);
    c.dailyBadge      = ToImVec4(g_Settings.dailyBadge);
    c.dailyBadgeText  = ToImVec4(g_Settings.dailyBadgeText);
    c.weeklyBadge     = ToImVec4(g_Settings.weeklyBadge);
    c.weeklyBadgeText = ToImVec4(g_Settings.weeklyBadgeText);
    g_Notes.SetPreviewEnabled(g_Settings.editPreview);
    g_Notes.SetListWidth(g_Settings.listWidth);
}

static void CollectSettings() {
    const Theme::UserColors& c = Theme::Colors();
    g_Settings.showBorder  = c.showBorder;
    g_Settings.border      = ToRgba(c.border);
    g_Settings.background  = { c.hasBackground, ToRgba(c.background) };
    g_Settings.text        = { c.hasText,       ToRgba(c.text) };
    g_Settings.groupName   = { c.hasGroupName,  ToRgba(c.groupName) };
    g_Settings.noGroup     = { c.hasNoGroup,    ToRgba(c.noGroup) };
    g_Settings.listBackground = { c.hasListBg,   ToRgba(c.listBg) };
    g_Settings.listText       = { c.hasListText, ToRgba(c.listText) };
    g_Settings.noteBackground = { c.hasNoteBg,   ToRgba(c.noteBg) };
    g_Settings.noteText       = { c.hasNoteText, ToRgba(c.noteText) };
    g_Settings.showListBorder = c.showListBorder;
    g_Settings.listBorder     = { c.hasListBorder, ToRgba(c.listBorder) };
    g_Settings.showNoteBorder = c.showNoteBorder;
    g_Settings.noteBorder     = { c.hasNoteBorder, ToRgba(c.noteBorder) };
    g_Settings.dailyBadge      = ToRgba(c.dailyBadge);
    g_Settings.dailyBadgeText  = ToRgba(c.dailyBadgeText);
    g_Settings.weeklyBadge     = ToRgba(c.weeklyBadge);
    g_Settings.weeklyBadgeText = ToRgba(c.weeklyBadgeText);
    g_Settings.editPreview = g_Notes.PreviewEnabled();
    g_Settings.listWidth   = g_Notes.ListWidth();
}

static void SaveSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    CollectSettings();
    Magpie::SafeFile::WriteAtomic(path, g_Settings.ToJson());   // crash-safe replace
}

static void LoadSettings() {
    const std::string path = SettingsPath();
    std::string content;
    if (!path.empty() && Magpie::SafeFile::Read(path, content) && !g_Settings.FromJson(content))
        Magpie::SafeFile::SetAside(path);   // malformed: keep it for inspection, use defaults
    ApplySettings();
}

// Add/remove the quick-access shortcut, keeping g_QAIconAdded in sync. The icon
// texture is loaded once at AddonLoad regardless; these only toggle the shortcut.
static void AddQAIcon() {
    if (!APIDefs || g_QAIconAdded) return;
    APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON, KB_TOGGLE, WINDOW_NAME);
    g_QAIconAdded = true;
}
static void RemoveQAIcon() {
    if (!APIDefs || !g_QAIconAdded) return;
    APIDefs->QuickAccess_Remove(QA_ID);
    g_QAIconAdded = false;
}

// ── Static addon definition ───────────────────────────────────────────────
static AddonDefinition_t AddonDef = {};

// ── DllMain ───────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD /*ul_reason*/, LPVOID /*lpReserved*/) {
    return TRUE;
}

// ── GetAddonDef (exported) ────────────────────────────────────────────────
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature        = 0x54504D6D;          // "TPMm" — distinct from Magpie Memo 0x4D61704D
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = WINDOW_NAME;
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author           = "PieOrCake.7635 & tobia-codes";
    AddonDef.Description      = "Notepad with markdown, checklists and rich chips. Based on Magpie Memo. Recommended: Decoder Ring.";
    AddonDef.Load             = AddonLoad;
    AddonDef.Unload           = AddonUnload;
    AddonDef.Flags            = AF_None;
    // No auto-update source yet: pointing at upstream would "update" this fork back to Magpie Memo.
    AddonDef.Provider         = UP_None;
    AddonDef.UpdateLink       = nullptr;

    return &AddonDef;
}

// ── AddonLoad ─────────────────────────────────────────────────────────────
// TODO: register a WndProc via APIDefs->WndProc_Register to call
//       Magpie::ChipInputActive / Magpie::RequestChipBlur for held-key edge
//       cases (e.g. WASD leaking to the game while a chip cell is focused).
//       This is deferred polish -- not required for the basic integration.
void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;

    // Share Nexus's ImGui context and allocators with this DLL's copy of ImGui.
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree);

    // Register render callbacks.
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    // Localization: the language probe (see NexusLang) and the keybind's display name
    // in Nexus's keybind list, in both supported languages.
    APIDefs->Localization_Set(LOC_LANG_PROBE, "en", "en");
    APIDefs->Localization_Set(LOC_LANG_PROBE, "de", "de");
    APIDefs->Localization_Set(KB_TOGGLE, "en", "Toggle Trailpack Memo");
    Magpie::I18n::SetLang(Magpie::I18n::Lang::De);
    APIDefs->Localization_Set(KB_TOGGLE, "de", Tr("Toggle Trailpack Memo"));
    Magpie::I18n::SetLang(Magpie::I18n::Lang::En);

    // Register toggle keybind (default CTRL+SHIFT+T; Magpie Memo keeps CTRL+SHIFT+M).
    APIDefs->InputBinds_RegisterWithString(KB_TOGGLE, ProcessKeybind, "CTRL+SHIFT+T");

    // Close window on Escape.
    APIDefs->GUI_RegisterCloseOnEscape(WINDOW_NAME, &g_WindowVisible);

    // Initialise notes store (loads from disk).
    g_Notes.Init(APIDefs);

    // Icon cache (async chip-icon download/upload via Nexus textures).
    Magpie::Icons::Init(APIDefs);

    // Decoder Ring consumer client (optional provider; lifetime-contract safe).
    Magpie::Decoder::Init(APIDefs);

    // Pie UI consumer client (optional provider; latched presence, no-op when absent).
    Magpie::PieUi::Init(APIDefs);

    // Load persisted settings (e.g. the quick-access toggle), then register the
    // quick-access icon from the embedded icon.png (same method as Alter Ego).
    LoadSettings();
    ApplyLanguage();
    APIDefs->Textures_LoadFromMemory(TEX_ICON, (void*)MAGPIE_ICON, MAGPIE_ICON_size, nullptr);
    if (g_Settings.showQaIcon) AddQAIcon();

    APIDefs->Log(LOGL_INFO, "TrailpackMemo", "Trailpack Memo loaded.");
}

// ── AddonUnload ───────────────────────────────────────────────────────────
void AddonUnload() {
    // Drop Decoder Ring subscriptions + cache before APIDefs goes away.
    Magpie::Decoder::Shutdown();

    // Unsubscribe Pie UI's READY handler before APIDefs goes away.
    Magpie::PieUi::Shutdown();

    // Mark the icon cache dead so a late texture-load callback can't touch torn-down state.
    Magpie::Icons::Shutdown();

    // Save notes before we lose APIDefs (Shutdown only writes to disk — safe).
    g_Notes.Shutdown();

    // Remove the quick-access shortcut if it was registered.
    RemoveQAIcon();

    APIDefs->GUI_DeregisterCloseOnEscape(WINDOW_NAME);
    APIDefs->InputBinds_Deregister(KB_TOGGLE);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    APIDefs = nullptr;
}

// ── AddonRender ───────────────────────────────────────────────────────────
void AddonRender() {
    if (!g_WindowVisible && !g_HelpVisible && !g_SettingsVisible) return;
    ApplyLanguage();   // Auto follows a Nexus language switch without a restart

    // Fonts for sharp scaling, read outside any window (see FontScale.h).
    {
        auto* nl = static_cast<NexusLinkData_t*>(APIDefs->DataLink_Get(DL_NEXUS_LINK));
        Magpie::SetFontBase(ImGui::GetFont(), nl != nullptr ? static_cast<ImFont*>(nl->FontBig) : nullptr);
    }

    if (g_WindowVisible) {
        Theme::PushWindowStyle();
        const bool open = ImGui::Begin(WINDOW_NAME, &g_WindowVisible);
        Help::TitleBarButton(WINDOW_NAME, &g_HelpVisible);   // "?" next to the title
        if (open) {
            // UI text size for everything inside the window (not the title bar).
            Magpie::FontScaleScope uiFont(g_Settings.uiTextScale / 100.0f);
            g_Notes.SetNoteTextScale(g_Settings.noteTextScale / 100.0f);
            g_Notes.Render();
            if (g_Notes.ConsumeSettingsChanged()) SaveSettings();
            if (g_Notes.ConsumeOpenSettings()) {
                g_SettingsVisible = true;
                ImGui::SetWindowFocus("###tpm_settings");
            }
        }
        ImGui::End();
        Theme::PopWindowStyle();
    }

    Help::Render(&g_HelpVisible, g_Settings.uiTextScale / 100.0f);

    // Own settings window: the same options as in Nexus (RenderOptions), so both stay in sync.
    if (g_SettingsVisible) {
        const std::string title = std::string(WINDOW_NAME) + " - " + Tr("Settings") + "###tpm_settings";
        ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
        Theme::PushWindowStyle();
        if (ImGui::Begin(title.c_str(), &g_SettingsVisible)) {
            Magpie::FontScaleScope uiFont(g_Settings.uiTextScale / 100.0f);
            RenderOptions();
        }
        ImGui::End();
        Theme::PopWindowStyle();
    }
}

// ── AddonOptions ──────────────────────────────────────────────────────────
// Nexus addon options pane; the settings window renders the same content.
void AddonOptions() {
    RenderOptions();
}

static void RenderOptions() {
    ApplyLanguage();

    // Language: Auto (= Nexus's language) or a fixed one. Language names stay native.
    {
        const char* items[] = { Tr("Auto (Nexus language)"), "English", "Deutsch" };
        int mode = (int)g_Settings.language;
        if (ImGui::Combo(Tr("Language"), &mode, items, IM_ARRAYSIZE(items))) {
            g_Settings.language = (Magpie::Settings::Language)mode;
            ApplyLanguage();
            SaveSettings();
        }
    }
    ImGui::Separator();

    // Live Decoder Ring presence — re-evaluated every frame, never latched.
    // Drives the chip experience: full names + icons + tooltips when present,
    // basic labels (still fully usable) when absent.
    if (Magpie::Decoder::Present()) {
        ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.50f, 1.0f),
            "%s", Tr("Decoder Ring: connected -- chips show full names and icons."));
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.78f, 0.45f, 1.0f),
            "%s", Tr("Decoder Ring not loaded -- chips show basic labels."));
        ImGui::TextDisabled("%s", Tr("Copy code and Open in wiki still work."));
    }

    ImGui::Separator();

    // Quick-access icon toggle (persisted; default on).
    if (ImGui::Checkbox(Tr("Show quick access icon"), &g_Settings.showQaIcon)) {
        if (g_Settings.showQaIcon) AddQAIcon();
        else              RemoveQAIcon();
        SaveSettings();
    }

    // Fonts and colours sit in their own collapsible sections below the general options
    // (Nexus draws the surrounding "Options" header; addons cannot add sibling headers).

    // ── Font options ──────────────────────────────────────────────────────
    // Text sizes (live; saved when a slider is released): the window's UI elements,
    // and -- independently -- the note content (view, editor, preview).
    if (ImGui::CollapsingHeader(Tr("Font options"))) {
        auto sizeSlider = [](const char* id, const char* label, int& pct) {
            ImGui::PushID(id);
            ImGui::SliderInt(label, &pct, Magpie::Settings::kScaleMin, Magpie::Settings::kScaleMax, "%d%%");
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset")) && pct != 100) {
                pct = 100;
                SaveSettings();
            }
            ImGui::PopID();
        };
        sizeSlider("ui_size",   Tr("UI text size"),   g_Settings.uiTextScale);
        sizeSlider("note_size", Tr("Note text size"), g_Settings.noteTextScale);
    }

    // ── Colour options ────────────────────────────────────────────────────
    // Picked live; saved once no picker is being dragged any more (not every frame).
    // The save check runs even while the section is collapsed. Rows are label | swatch
    // (click: picker with RGBA + alpha) | Reset, in fixed columns so nothing gets cut off.
    static bool s_ColorsDirty = false;
    if (ImGui::CollapsingHeader(Tr("Colour options"))) {
        Theme::UserColors& c = Theme::Colors();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
        const float swatchW = ImGui::GetFrameHeight();
        const float badgeW  = ImGui::CalcTextSize("W").x + ImGui::GetFontSize() * 0.5f;

        // Column positions: right of the widest label (badge rows: preview + name).
        float labelW = 0.0f;
        for (const char* l : { Tr("Border"), Tr("Background"), Tr("Text"), Tr("Group names"), Tr("No group") })
            labelW = std::max(labelW, ImGui::CalcTextSize(l).x);
        for (Magpie::NoteType t : { Magpie::NoteType::Daily, Magpie::NoteType::Weekly })
            labelW = std::max(labelW, badgeW + style.ItemSpacing.x + ImGui::CalcTextSize(NotesUi::TypeName(t)).x);
        ImGui::Indent();
        const float colSwatch = ImGui::GetCursorPosX() + labelW + style.ItemSpacing.x * 2.0f;
        // Badge table: background and text swatches side by side, each under its header.
        const float colText  = colSwatch + std::max(swatchW, ImGui::CalcTextSize(Tr("Background")).x) + style.ItemSpacing.x * 2.0f;
        const float colReset = colText   + std::max(swatchW, ImGui::CalcTextSize(Tr("Text")).x) + style.ItemSpacing.x * 2.0f;
        ImGui::Unindent();

        auto label = [&](const char* text) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
            ImGui::SameLine(colSwatch);
        };
        auto resetTip = [] {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("Reset uses the Nexus default (gold for the border)."));
        };
        // A colour that follows `fallback` until picked (has = false).
        auto optionalRow = [&](const char* id, const char* text, bool& has, ImVec4& value, const ImVec4& fallback) {
            ImGui::PushID(id);
            label(text);
            ImVec4 v = has ? value : fallback;
            if (ImGui::ColorEdit4("##c", &v.x, flags)) { value = v; has = true; s_ColorsDirty = true; }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset"))) { has = false; s_ColorsDirty = true; }
            resetTip();
            ImGui::PopID();
        };
        auto heading = [](const char* text) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", text);
        };

        heading(Tr("Window"));
        ImGui::Indent();
        // Same label in three sections: the id is scoped per section.
        auto borderToggle = [&](const char* id, bool& show) {
            ImGui::PushID(id);
            if (ImGui::Checkbox(Tr("Show border"), &show)) s_ColorsDirty = true;
            ImGui::PopID();
        };
        borderToggle("window_border", c.showBorder);
        ImGui::PushID("border");
        label(Tr("Border"));
        if (ImGui::ColorEdit4("##c", &c.border.x, flags)) s_ColorsDirty = true;
        ImGui::SameLine();
        if (ImGui::Button(Tr("Reset"))) { c.border = Theme::BORDER_GOLD; s_ColorsDirty = true; }
        resetTip();
        ImGui::PopID();
        // Background / text show Nexus's current colour until the user picks their own.
        optionalRow("background", Tr("Background"), c.hasBackground, c.background, style.Colors[ImGuiCol_WindowBg]);
        optionalRow("text",       Tr("Text"),       c.hasText,       c.text,       style.Colors[ImGuiCol_Text]);
        ImGui::Unindent();

        // List / note panes: Nexus's child background and the window text until picked.
        const ImVec4 paneBg   = style.Colors[ImGuiCol_ChildBg];
        const ImVec4 paneText = style.Colors[ImGuiCol_Text];

        heading(Tr("Note list"));
        ImGui::Indent();
        borderToggle("list_border_on", c.showListBorder);
        optionalRow("list_border", Tr("Border"),   c.hasListBorder, c.listBorder, c.border);
        optionalRow("list_bg",   Tr("Background"), c.hasListBg,   c.listBg,   paneBg);
        optionalRow("list_text", Tr("Text"),       c.hasListText, c.listText, paneText);
        // Group names: follow the border colour until picked; single groups can override
        // this via right-click -> "Group colour...".
        optionalRow("group_name", Tr("Group names"), c.hasGroupName, c.groupName, c.border);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Group names follow the border until set; right-click a group for its own colour."));
        // "No group" label: Nexus's disabled-text colour until picked.
        optionalRow("no_group", Tr("No group"), c.hasNoGroup, c.noGroup, style.Colors[ImGuiCol_TextDisabled]);
        ImGui::Unindent();

        // Daily / weekly badges: preview + name | background | letter | Reset (both).
        heading(Tr("Note"));
        ImGui::Indent();
        borderToggle("note_border_on", c.showNoteBorder);
        optionalRow("note_border", Tr("Border"),   c.hasNoteBorder, c.noteBorder, c.border);
        optionalRow("note_bg",   Tr("Background"), c.hasNoteBg,   c.noteBg,   paneBg);
        optionalRow("note_text", Tr("Text"),       c.hasNoteText, c.noteText, paneText);
        ImGui::Unindent();

        heading(Tr("Daily / weekly badges"));
        ImGui::Indent();
        ImGui::SetCursorPosX(colSwatch); ImGui::TextDisabled("%s", Tr("Background"));
        ImGui::SameLine(colText);   ImGui::TextDisabled("%s", Tr("Text"));
        auto badgeRow = [&](Magpie::NoteType t, ImVec4& bgCol, ImVec4& textCol, const ImVec4& bgDef) {
            ImGui::PushID((int)t);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            NotesUi::DrawTypeBadge(ImGui::GetWindowDrawList(), p, ImGui::GetFrameHeight(), t);
            ImGui::Dummy(ImVec2(badgeW, ImGui::GetFrameHeight()));
            ImGui::SameLine();
            label(NotesUi::TypeName(t));
            if (ImGui::ColorEdit4("##bg", &bgCol.x, flags)) s_ColorsDirty = true;
            ImGui::SameLine(colText);
            if (ImGui::ColorEdit4("##text", &textCol.x, flags)) s_ColorsDirty = true;
            ImGui::SameLine(colReset);
            if (ImGui::Button(Tr("Reset"))) { bgCol = bgDef; textCol = Theme::BADGE_TEXT; s_ColorsDirty = true; }
            ImGui::PopID();
        };
        badgeRow(Magpie::NoteType::Daily,  c.dailyBadge,  c.dailyBadgeText,  Theme::BADGE_DAILY);
        badgeRow(Magpie::NoteType::Weekly, c.weeklyBadge, c.weeklyBadgeText, Theme::BADGE_WEEKLY);
        ImGui::Unindent();

        ImGui::Spacing();
        if (ImGui::Button(Tr("Reset all colours"))) {
            c = Theme::UserColors();
            s_ColorsDirty = true;
        }
    }

    if (s_ColorsDirty && !ImGui::IsAnyItemActive()) {
        SaveSettings();
        s_ColorsDirty = false;
    }
}

// ── ProcessKeybind ────────────────────────────────────────────────────────
void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, KB_TOGGLE) == 0) {
        g_WindowVisible = !g_WindowVisible;
    }
}
