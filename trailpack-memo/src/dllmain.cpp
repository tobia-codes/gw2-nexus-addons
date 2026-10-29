// dllmain.cpp — Trailpack Memo Nexus addon lifecycle.
// Fork of PieOrCake's Magpie Memo (MIT) with task-list checkboxes and a markdown toolbar.
// Stage 2: two-pane notes shell + JSON persistence.

#include <windows.h>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>

#include "nexus/Nexus.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include "Theme.h"
#include "NotesWindow.h"
#include "DecoderClient.h"
#include "PieUiClient.h"
#include "I18n.h"
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
bool             g_ShowQAIcon    = true;   // show the quick-access icon (persisted; default on)
bool             g_QAIconAdded   = false;  // tracks whether the shortcut is currently registered
int              g_TextScalePct  = 100;    // window text size in percent (persisted; 100 = Nexus default)

// UI language: Auto follows Nexus's language setting (German if Nexus is set to German,
// English otherwise); En/De force one. Persisted as "language": "auto" | "en" | "de".
enum class LangMode { Auto, En, De };
LangMode         g_LangMode      = LangMode::Auto;
NotesWindow      g_Notes;

// ── Forward declarations ──────────────────────────────────────────────────
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();

using Magpie::I18n::Tr;

static const char* LangModeKey(LangMode m) {
    return m == LangMode::En ? "en" : m == LangMode::De ? "de" : "auto";
}

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
    switch (g_LangMode) {
        case LangMode::En:   Magpie::I18n::SetLang(Lang::En); break;
        case LangMode::De:   Magpie::I18n::SetLang(Lang::De); break;
        case LangMode::Auto: Magpie::I18n::SetLang(NexusLang()); break;
    }
}

static constexpr int kTextScaleMin = 70;
static constexpr int kTextScaleMax = 200;

// Colours are stored as [r, g, b, a] (0..1). An absent or null background/text
// entry means "use Nexus's default style".
static nlohmann::json ColorToJson(const ImVec4& c) { return { c.x, c.y, c.z, c.w }; }
static bool ColorFromJson(const nlohmann::json& j, ImVec4& out) {
    if (!j.is_array() || j.size() != 4) return false;
    float v[4];
    for (int i = 0; i < 4; ++i) {
        if (!j[i].is_number()) return false;
        const float f = j[i].get<float>();
        v[i] = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    out = ImVec4(v[0], v[1], v[2], v[3]);
    return true;
}

// ── Settings persistence (settings.json in the addon data dir) ────────────
static std::string SettingsPath() {
    if (!APIDefs) return std::string();
    const char* dir = APIDefs->Paths_GetAddonDirectory("TrailpackMemo");
    if (!dir || !*dir) return std::string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (std::filesystem::path(dir) / "settings.json").string();
}

static void SaveSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    nlohmann::json j;
    j["show_qa_icon"] = g_ShowQAIcon;
    j["edit_preview"] = g_Notes.PreviewEnabled();
    j["text_scale"]   = g_TextScalePct;
    j["language"]     = LangModeKey(g_LangMode);
    const Theme::UserColors& c = Theme::Colors();
    j["color_border"]     = ColorToJson(c.border);
    j["color_background"] = c.hasBackground ? ColorToJson(c.background) : nlohmann::json();
    j["color_text"]       = c.hasText       ? ColorToJson(c.text)       : nlohmann::json();
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (f.is_open()) f << j.dump(2);
}

static void LoadSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    nlohmann::json j;
    try { j = nlohmann::json::parse(f); }
    catch (...) { return; }   // malformed: keep defaults
    if (j.contains("show_qa_icon") && j["show_qa_icon"].is_boolean())
        g_ShowQAIcon = j["show_qa_icon"].get<bool>();
    if (j.contains("edit_preview") && j["edit_preview"].is_boolean())
        g_Notes.SetPreviewEnabled(j["edit_preview"].get<bool>());
    if (j.contains("text_scale") && j["text_scale"].is_number_integer()) {
        const int pct = j["text_scale"].get<int>();
        g_TextScalePct = pct < kTextScaleMin ? kTextScaleMin : pct > kTextScaleMax ? kTextScaleMax : pct;
    }
    if (j.contains("language") && j["language"].is_string()) {
        const std::string lang = j["language"].get<std::string>();
        g_LangMode = lang == "en" ? LangMode::En : lang == "de" ? LangMode::De : LangMode::Auto;
    }
    Theme::UserColors& c = Theme::Colors();
    if (j.contains("color_border")) ColorFromJson(j["color_border"], c.border);
    if (j.contains("color_background"))
        c.hasBackground = ColorFromJson(j["color_background"], c.background);
    if (j.contains("color_text"))
        c.hasText = ColorFromJson(j["color_text"], c.text);
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
    if (g_ShowQAIcon) AddQAIcon();

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
    if (!g_WindowVisible) return;
    ApplyLanguage();   // Auto follows a Nexus language switch without a restart

    Theme::PushWindowStyle();

    if (ImGui::Begin(WINDOW_NAME, &g_WindowVisible)) {
        // Scales everything inside the window (child panes inherit it), not the title bar.
        ImGui::SetWindowFontScale(g_TextScalePct / 100.0f);
        g_Notes.Render();
        if (g_Notes.ConsumeSettingsChanged()) SaveSettings();
    }
    ImGui::End();

    Theme::PopWindowStyle();
}

// ── AddonOptions ──────────────────────────────────────────────────────────
void AddonOptions() {
    ApplyLanguage();

    // Language: Auto (= Nexus's language) or a fixed one. Language names stay native.
    {
        const char* items[] = { Tr("Auto (Nexus language)"), "English", "Deutsch" };
        int mode = (int)g_LangMode;
        if (ImGui::Combo(Tr("Language"), &mode, items, IM_ARRAYSIZE(items))) {
            g_LangMode = (LangMode)mode;
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
    if (ImGui::Checkbox(Tr("Show quick access icon"), &g_ShowQAIcon)) {
        if (g_ShowQAIcon) AddQAIcon();
        else              RemoveQAIcon();
        SaveSettings();
    }

    // Text size of the notes window (live; saved when the slider is released).
    ImGui::SliderInt(Tr("Text size"), &g_TextScalePct, kTextScaleMin, kTextScaleMax, "%d%%");
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
    ImGui::SameLine();
    ImGui::PushID("text_size");
    const bool resetSize = ImGui::Button(Tr("Reset"));
    ImGui::PopID();
    if (resetSize && g_TextScalePct != 100) {
        g_TextScalePct = 100;
        SaveSettings();
    }

    // ── Colours ───────────────────────────────────────────────────────────
    // Picked live; saved once no picker is being dragged any more (not every frame).
    static bool s_ColorsDirty = false;
    Theme::UserColors& c = Theme::Colors();
    const ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;

    ImGui::Separator();
    ImGui::TextUnformatted(Tr("Window colours"));

    ImGui::PushID("border");
    if (ImGui::ColorEdit4(Tr("Border"), &c.border.x, flags)) s_ColorsDirty = true;
    ImGui::SameLine();
    if (ImGui::Button(Tr("Reset"))) { c.border = Theme::BORDER_GOLD; s_ColorsDirty = true; }
    ImGui::PopID();

    // Background / text show Nexus's current colour until the user picks their own.
    ImGui::PushID("background");
    ImVec4 bg = c.hasBackground ? c.background : ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    if (ImGui::ColorEdit4(Tr("Background"), &bg.x, flags)) {
        c.background = bg; c.hasBackground = true; s_ColorsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Reset"))) { c.hasBackground = false; s_ColorsDirty = true; }
    ImGui::PopID();

    ImGui::PushID("text");
    ImVec4 tx = c.hasText ? c.text : ImGui::GetStyle().Colors[ImGuiCol_Text];
    if (ImGui::ColorEdit4(Tr("Text"), &tx.x, flags)) {
        c.text = tx; c.hasText = true; s_ColorsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Reset"))) { c.hasText = false; s_ColorsDirty = true; }
    ImGui::PopID();

    ImGui::TextDisabled("%s", Tr("Reset uses the Nexus default (gold for the border)."));

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
