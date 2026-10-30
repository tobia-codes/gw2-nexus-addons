// dllmain.cpp — Trailpack Memo Nexus addon lifecycle.
// Notepad with markdown, task-list checkboxes, chat-link and event chips.

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <future>

#include "nexus/Nexus.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ui/Theme.h"
#include "notes/NotesWindow.h"
#include "integrations/DecoderClient.h"
#include "integrations/PieUiClient.h"
#include "core/I18n.h"
#include "ui/FontScale.h"
#include "core/SafeFile.h"
#include "core/Settings.h"
#include "core/Backup.h"
#include "ui/FileDialog.h"
#include "ui/HelpWindow.h"
#include "notes/NotesUi.h"
#include "markdown/MarkdownRender.h"
#include "chips/IconCache.h"
#include "generated/EmbeddedIcon.h"   // TRAILPACK_ICON[] + TRAILPACK_ICON_size (generated from icon.png)

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
Trailpack::Settings g_Settings;               // settings.json (see Settings.h); colours, preview
                                           // and list width live in Theme / g_Notes meanwhile
NotesWindow      g_Notes;

// Replaces libstdc++'s verbose std::terminate handler, which prints the demangled exception
// type to stderr (invisible in the game anyway) and links in the ~50 KB C++ demangler.
namespace __gnu_cxx {
void __verbose_terminate_handler() { std::abort(); }
}

// ── Forward declarations ──────────────────────────────────────────────────
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();
static void RenderOptions();

using Trailpack::I18n::Tr;

// Language Nexus is currently set to, read through a probe key registered at load
// (Localization_Set for "en" and "de"). Any other Nexus language returns the key
// itself -> English.
static Trailpack::I18n::Lang NexusLang() {
    if (!APIDefs || !APIDefs->Localization_Translate) return Trailpack::I18n::Lang::En;
    const char* code = APIDefs->Localization_Translate(LOC_LANG_PROBE);
    return (code && strcmp(code, "de") == 0) ? Trailpack::I18n::Lang::De : Trailpack::I18n::Lang::En;
}

// Resolve the setting to the language used this frame (cheap; called per render).
static void ApplyLanguage() {
    using Trailpack::I18n::Lang;
    switch (g_Settings.language) {
        case Trailpack::Settings::Language::En:   Trailpack::I18n::SetLang(Lang::En); break;
        case Trailpack::Settings::Language::De:   Trailpack::I18n::SetLang(Lang::De); break;
        case Trailpack::Settings::Language::Auto: Trailpack::I18n::SetLang(NexusLang()); break;
    }
}

// ── Settings persistence (settings.json in the addon data dir) ────────────
// g_Settings is the file's model; the live colours (Theme::Colors), the preview toggle and
// the list width (g_Notes) are copied in on load and collected again before saving.

static ImVec4 ToImVec4(const Trailpack::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }
static Trailpack::Rgba ToRgba(const ImVec4& c)   { return Trailpack::Rgba{ c.x, c.y, c.z, c.w }; }

static std::string SettingsPath() {
    if (!APIDefs) return std::string();
    const char* dir = APIDefs->Paths_GetAddonDirectory("TrailpackMemo");
    if (!dir || !*dir) return std::string();
    Trailpack::SafeFile::CreateDirectories(dir);
    return Trailpack::SafeFile::Join(dir, "settings.json");
}

// Live colours (Theme::Colors) <-> a ColorSet (settings.json, themes).
static void ApplyColors(const Trailpack::ColorSet& s) {
    Theme::UserColors& c = Theme::Colors();
    c.showBorder      = s.showBorder;
    c.border          = ToImVec4(s.border);
    c.hasBackground   = s.background.set;
    c.background      = ToImVec4(s.background.color);
    c.hasText         = s.text.set;
    c.text            = ToImVec4(s.text.color);
    c.hasGroupName    = s.groupName.set;
    c.groupName       = ToImVec4(s.groupName.color);
    c.hasListBg       = s.listBackground.set;
    c.listBg          = ToImVec4(s.listBackground.color);
    c.hasListText     = s.listText.set;
    c.listText        = ToImVec4(s.listText.color);
    c.hasNoteBg       = s.noteBackground.set;
    c.noteBg          = ToImVec4(s.noteBackground.color);
    c.hasNoteText     = s.noteText.set;
    c.noteText        = ToImVec4(s.noteText.color);
    c.showListBorder  = s.showListBorder;
    c.hasListBorder   = s.listBorder.set;
    c.listBorder      = ToImVec4(s.listBorder.color);
    c.showNoteBorder  = s.showNoteBorder;
    c.hasNoteBorder   = s.noteBorder.set;
    c.noteBorder      = ToImVec4(s.noteBorder.color);
    c.showEditorBorder = s.showEditorBorder;
    c.hasEditorBorder = s.editorBorder.set;
    c.editorBorder    = ToImVec4(s.editorBorder.color);
    c.hasMenuButton   = s.menuButton.set;
    c.menuButton      = ToImVec4(s.menuButton.color);
    c.hasMenuIcon     = s.menuIcon.set;
    c.menuIcon        = ToImVec4(s.menuIcon.color);
    c.hasPopupBg      = s.popupBackground.set;
    c.popupBg         = ToImVec4(s.popupBackground.color);
    c.hasPopupText    = s.popupText.set;
    c.popupText       = ToImVec4(s.popupText.color);
    c.dailyBadge      = ToImVec4(s.dailyBadge);
    c.dailyBadgeText  = ToImVec4(s.dailyBadgeText);
    c.weeklyBadge     = ToImVec4(s.weeklyBadge);
    c.weeklyBadgeText = ToImVec4(s.weeklyBadgeText);
}

static Trailpack::ColorSet CollectColors() {
    const Theme::UserColors& c = Theme::Colors();
    Trailpack::ColorSet s;
    s.showBorder      = c.showBorder;
    s.border          = ToRgba(c.border);
    s.background      = { c.hasBackground, ToRgba(c.background) };
    s.text            = { c.hasText,       ToRgba(c.text) };
    s.groupName       = { c.hasGroupName,  ToRgba(c.groupName) };
    s.listBackground  = { c.hasListBg,     ToRgba(c.listBg) };
    s.listText        = { c.hasListText,   ToRgba(c.listText) };
    s.noteBackground  = { c.hasNoteBg,     ToRgba(c.noteBg) };
    s.noteText        = { c.hasNoteText,   ToRgba(c.noteText) };
    s.showListBorder  = c.showListBorder;
    s.listBorder      = { c.hasListBorder, ToRgba(c.listBorder) };
    s.showNoteBorder  = c.showNoteBorder;
    s.noteBorder      = { c.hasNoteBorder, ToRgba(c.noteBorder) };
    s.showEditorBorder = c.showEditorBorder;
    s.editorBorder    = { c.hasEditorBorder, ToRgba(c.editorBorder) };
    s.menuButton      = { c.hasMenuButton, ToRgba(c.menuButton) };
    s.menuIcon        = { c.hasMenuIcon,   ToRgba(c.menuIcon) };
    s.popupBackground = { c.hasPopupBg,    ToRgba(c.popupBg) };
    s.popupText       = { c.hasPopupText,  ToRgba(c.popupText) };
    s.dailyBadge      = ToRgba(c.dailyBadge);
    s.dailyBadgeText  = ToRgba(c.dailyBadgeText);
    s.weeklyBadge     = ToRgba(c.weeklyBadge);
    s.weeklyBadgeText = ToRgba(c.weeklyBadgeText);
    return s;
}

static void ApplySettings() {
    ApplyColors(g_Settings.colors);
    g_Notes.SetPreviewEnabled(g_Settings.editPreview);
    g_Notes.SetListWidth(g_Settings.listWidth);
    g_Notes.SetGroupsFirst(g_Settings.groupsFirst);
    g_Notes.SetRowSpacing(g_Settings.rowSpacing);
}

static void CollectSettings() {
    g_Settings.colors      = CollectColors();
    g_Settings.editPreview = g_Notes.PreviewEnabled();
    g_Settings.listWidth   = g_Notes.ListWidth();
}

static void SaveSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    CollectSettings();
    Trailpack::SafeFile::WriteAtomic(path, g_Settings.ToJson());   // crash-safe replace
}

static void LoadSettings() {
    const std::string path = SettingsPath();
    std::string content;
    if (!path.empty() && Trailpack::SafeFile::Read(path, content) && !g_Settings.FromJson(content))
        Trailpack::SafeFile::SetAside(path);   // malformed: keep it for inspection, use defaults
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

// ── Backup and settings export (Settings > Backup / Export / import settings) ──
// A backup is one file with all notes and all settings; a settings export has the settings
// only (Backup.h). The Windows file dialog runs on a worker thread and is polled by
// RenderOptions (one dialog at a time); restoring / importing is confirmed inline first.
enum class FileJob { CreateBackup, RestoreBackup, ExportSettings, ImportSettings };
static std::future<std::wstring> s_BackupDialog;   // valid while a dialog is open
static FileJob     s_FileJob = FileJob::CreateBackup;
static std::string s_BackupMessage;                 // result line under the backup buttons
static std::string s_RestoreFile;                   // pending restore (confirm first)
static std::string s_RestoreNotes, s_RestoreSettings;
static int         s_RestoreNoteCount = 0;
static std::string s_SettingsMessage;               // result line under the settings buttons
static std::string s_ImportSettingsFile;            // pending settings import (confirm first)
static std::string s_ImportSettings;

static std::string BackupDir() {
    const std::string data = g_Notes.DataDir();
    return data.empty() ? data : Trailpack::SafeFile::Join(data, "backups");
}

static void StartFileDialog(FileJob job) {
    if (s_BackupDialog.valid()) return;   // one dialog at a time
    const std::string dir = BackupDir();
    if (!dir.empty()) Trailpack::SafeFile::CreateDirectories(dir);

    const bool backup = job == FileJob::CreateBackup || job == FileJob::RestoreBackup;
    const bool save   = job == FileJob::CreateBackup || job == FileJob::ExportSettings;
    char name[64] = "";
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof name, backup ? "trailpack-memo-backup-%Y-%m-%d.json"
                                            : "trailpack-memo-settings-%Y-%m-%d.json", std::localtime(&now));
    const char* title = job == FileJob::CreateBackup   ? Tr("Create backup")
                      : job == FileJob::RestoreBackup  ? Tr("Restore backup")
                      : job == FileJob::ExportSettings ? Tr("Export settings")
                                                       : Tr("Import settings");

    s_FileJob = job;
    s_BackupMessage.clear();
    s_RestoreFile.clear();
    s_SettingsMessage.clear();
    s_ImportSettingsFile.clear();
    s_BackupDialog = std::async(std::launch::async, FileDialog::Run, save, FileDialog::Widen(dir),
                                save ? FileDialog::Widen(name) : std::wstring(),
                                FileDialog::Widen(title),
                                FileDialog::JsonFilter(backup ? Tr("Trailpack Memo backup (*.json)")
                                                              : Tr("Trailpack Memo settings (*.json)")));
}

static void PollBackupDialog() {
    if (!s_BackupDialog.valid() ||
        s_BackupDialog.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const std::string path = FileDialog::Narrow(s_BackupDialog.get());
    if (path.empty()) return;   // cancelled

    const std::string fileName = Trailpack::SafeFile::FileName(path);
    char msg[512];
    if (s_FileJob == FileJob::ExportSettings) {
        CollectSettings();
        const std::string text = Trailpack::Backup::MakeSettings(g_Settings.ToJson(), (int64_t)std::time(nullptr));
        const bool ok = !text.empty() && Trailpack::SafeFile::WriteAtomic(path, text);
        snprintf(msg, sizeof msg, ok ? Tr("Settings saved as %s.") : Tr("Could not write %s."),
                 fileName.c_str());
        s_SettingsMessage = msg;
        return;
    }
    if (s_FileJob == FileJob::ImportSettings) {
        // Validate now, replace only after the user confirmed. A full backup works too.
        std::string text, settings;
        Trailpack::Settings check;
        if (!Trailpack::SafeFile::Read(path, text) || !Trailpack::Backup::ParseSettings(text, settings) ||
            !check.FromJson(settings)) {
            snprintf(msg, sizeof msg, Tr("%s contains no Trailpack Memo settings. Nothing was changed."),
                     fileName.c_str());
            s_SettingsMessage = msg;
            return;
        }
        s_ImportSettingsFile = fileName;
        s_ImportSettings     = std::move(settings);
        return;
    }
    if (s_FileJob == FileJob::CreateBackup) {
        CollectSettings();
        const std::string text = Trailpack::Backup::Make(g_Notes.NotesJson(), g_Settings.ToJson(),
                                                      (int64_t)std::time(nullptr));
        const bool ok = !text.empty() && Trailpack::SafeFile::WriteAtomic(path, text);
        snprintf(msg, sizeof msg, ok ? Tr("Backup saved as %s.") : Tr("Could not write %s."),
                 fileName.c_str());
        s_BackupMessage = msg;
        return;
    }

    // Restore: validate everything now, replace only after the user confirmed.
    std::string text, notes, settings;
    Trailpack::NotesStore checkNotes;
    Trailpack::Settings   checkSettings;
    if (!Trailpack::SafeFile::Read(path, text) || !Trailpack::Backup::Parse(text, notes, settings) ||
        !checkNotes.FromJson(notes) || !checkSettings.FromJson(settings)) {
        snprintf(msg, sizeof msg, Tr("%s is not a Trailpack Memo backup. Nothing was changed."),
                 fileName.c_str());
        s_BackupMessage = msg;
        return;
    }
    s_RestoreFile      = fileName;
    s_RestoreNotes     = std::move(notes);
    s_RestoreSettings  = std::move(settings);
    s_RestoreNoteCount = (int)checkNotes.Notes().size();
}

// Replace all settings with `json` (from defaults: a backup / settings export is complete),
// apply them live and save.
static void ReplaceSettings(const std::string& json) {
    Trailpack::Settings restored;
    restored.FromJson(json);
    g_Settings = restored;
    ApplySettings();
    ApplyLanguage();
    if (g_Settings.showQaIcon) AddQAIcon();
    else                       RemoveQAIcon();
    SaveSettings();
}

static void ImportSettings() {
    char msg[512];
    ReplaceSettings(s_ImportSettings);
    snprintf(msg, sizeof msg, Tr("Settings imported from %s."), s_ImportSettingsFile.c_str());
    s_SettingsMessage = msg;
    s_ImportSettingsFile.clear();
    s_ImportSettings.clear();
}

static void RestoreBackup() {
    char msg[256];
    if (!g_Notes.RestoreNotes(s_RestoreNotes)) {
        s_BackupMessage = Tr("The backup could not be restored. Nothing was changed.");
    } else {
        ReplaceSettings(s_RestoreSettings);
        snprintf(msg, sizeof msg, Trailpack::I18n::TrN(s_RestoreNoteCount, "Backup restored: %d note.",
                 "Backup restored: %d notes."), s_RestoreNoteCount);
        s_BackupMessage = msg;
    }
    s_RestoreFile.clear();
    s_RestoreNotes.clear();
    s_RestoreSettings.clear();
}

// The settings window's ImGui title in the current language (stable "###tpm_settings" id).
static std::string SettingsTitle() {
    return std::string(WINDOW_NAME) + " - " + Tr("Settings") + "###tpm_settings";
}

// Close-on-Escape names for the settings and guide windows. Nexus matches the full window
// title, which is translated, so both languages are registered (built once at load; the
// strings must stay alive while registered).
static std::string s_EscapeTitles[4];

// ── Static addon definition ───────────────────────────────────────────────
static AddonDefinition_t AddonDef = {};

// ── DllMain ───────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD /*ul_reason*/, LPVOID /*lpReserved*/) {
    return TRUE;
}

// ── GetAddonDef (exported) ────────────────────────────────────────────────
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature        = 0x54504D6D;          // "TPMm"
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = WINDOW_NAME;
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author           = "tobia-codes";
    AddonDef.Description      = "Notepad with markdown, checklists, event timers and rich chips. Recommended: Decoder Ring.";
    AddonDef.Load             = AddonLoad;
    AddonDef.Unload           = AddonUnload;
    AddonDef.Flags            = AF_None;
    // No auto-update source yet (see PLAN backlog B2).
    AddonDef.Provider         = UP_None;
    AddonDef.UpdateLink       = nullptr;

    return &AddonDef;
}

// ── AddonLoad ─────────────────────────────────────────────────────────────
// TODO: register a WndProc via APIDefs->WndProc_Register to call
//       Trailpack::ChipInputActive / Trailpack::RequestChipBlur for held-key edge
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
    Trailpack::I18n::SetLang(Trailpack::I18n::Lang::De);
    APIDefs->Localization_Set(KB_TOGGLE, "de", Tr("Toggle Trailpack Memo"));
    s_EscapeTitles[0] = SettingsTitle();
    s_EscapeTitles[1] = Help::Title();
    Trailpack::I18n::SetLang(Trailpack::I18n::Lang::En);
    s_EscapeTitles[2] = SettingsTitle();
    s_EscapeTitles[3] = Help::Title();

    // Register toggle keybind (default CTRL+SHIFT+T).
    APIDefs->InputBinds_RegisterWithString(KB_TOGGLE, ProcessKeybind, "CTRL+SHIFT+T");

    // Close windows on Escape (settings / guide: in both languages, see s_EscapeTitles).
    APIDefs->GUI_RegisterCloseOnEscape(WINDOW_NAME, &g_WindowVisible);
    for (int i = 0; i < 4; ++i)
        APIDefs->GUI_RegisterCloseOnEscape(s_EscapeTitles[i].c_str(), i % 2 == 0 ? &g_SettingsVisible
                                                                                 : &g_HelpVisible);

    // Initialise notes store (loads from disk).
    g_Notes.Init(APIDefs);

    // Icon cache (async chip-icon download/upload via Nexus textures).
    Trailpack::Icons::Init(APIDefs);

    // Decoder Ring consumer client (optional provider; lifetime-contract safe).
    Trailpack::Decoder::Init(APIDefs);

    // Pie UI consumer client (optional provider; latched presence, no-op when absent).
    Trailpack::PieUi::Init(APIDefs);

    // Load persisted settings (e.g. the quick-access toggle), then register the
    // quick-access icon from the embedded icon.png (same method as Alter Ego).
    LoadSettings();
    ApplyLanguage();
    APIDefs->Textures_LoadFromMemory(TEX_ICON, (void*)TRAILPACK_ICON, TRAILPACK_ICON_size, nullptr);
    if (g_Settings.showQaIcon) AddQAIcon();

    APIDefs->Log(LOGL_INFO, "TrailpackMemo", "Trailpack Memo loaded.");
}

// ── AddonUnload ───────────────────────────────────────────────────────────
void AddonUnload() {
    // A backup dialog still open on its worker thread must finish before the DLL goes away.
    if (s_BackupDialog.valid()) s_BackupDialog.wait();

    // Drop Decoder Ring subscriptions + cache before APIDefs goes away.
    Trailpack::Decoder::Shutdown();

    // Unsubscribe Pie UI's READY handler before APIDefs goes away.
    Trailpack::PieUi::Shutdown();

    // Mark the icon cache dead so a late texture-load callback can't touch torn-down state.
    Trailpack::Icons::Shutdown();

    // Save notes before we lose APIDefs (Shutdown only writes to disk — safe).
    g_Notes.Shutdown();

    // Remove the quick-access shortcut if it was registered.
    RemoveQAIcon();

    APIDefs->GUI_DeregisterCloseOnEscape(WINDOW_NAME);
    for (const std::string& t : s_EscapeTitles) APIDefs->GUI_DeregisterCloseOnEscape(t.c_str());
    APIDefs->InputBinds_Deregister(KB_TOGGLE);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    APIDefs = nullptr;
}

// ── AddonRender ───────────────────────────────────────────────────────────
void AddonRender() {
    if (!g_WindowVisible && !g_HelpVisible && !g_SettingsVisible) return;
    ApplyLanguage();   // Auto follows a Nexus language switch without a restart
    Theme::SetRounding(g_Settings.cornerRounding);   // note, settings and guide windows
    // ImGui's text cursor shows up as a gear in GW2: our text fields keep the normal game
    // cursor. Only a TextInput cursor set by our windows is replaced (not another addon's).
    const bool textCursorBefore = ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput;
    struct TextCursorFix {
        bool before;
        ~TextCursorFix() {
            if (!before && ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
    } textCursorFix{ textCursorBefore };

    // Fonts for sharp scaling, read outside any window (see FontScale.h).
    {
        auto* nl = static_cast<NexusLinkData_t*>(APIDefs->DataLink_Get(DL_NEXUS_LINK));
        Trailpack::SetFontBase(ImGui::GetFont(), nl != nullptr ? static_cast<ImFont*>(nl->FontBig) : nullptr);
    }

    if (g_WindowVisible) {
        Trailpack::SetCheckedTaskChips(g_Settings.doneShowTime, g_Settings.doneShowPopups);
        Theme::PushWindowStyle();
        const bool open = Theme::BeginWindow(WINDOW_NAME, &g_WindowVisible);
        Help::TitleBarButton(WINDOW_NAME, &g_HelpVisible);   // "?" next to the title
        if (open) {
            // UI text size for everything inside the window (not the title bar).
            Trailpack::FontScaleScope uiFont(g_Settings.uiTextScale / 100.0f);
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
    // Plain Nexus style (no user colours), so it stays readable whatever theme is picked;
    // only the background is made opaque and the corner rounding applies.
    if (g_SettingsVisible) {
        Theme::PlainWindowScope plain;
        const std::string title = SettingsTitle();
        ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (Theme::BeginWindow(title.c_str(), &g_SettingsVisible)) {
            Trailpack::FontScaleScope uiFont(g_Settings.uiTextScale / 100.0f);
            RenderOptions();
        }
        ImGui::End();
    }
}

// ── AddonOptions ──────────────────────────────────────────────────────────
// Nexus addon options pane; the settings window renders the same content.
void AddonOptions() {
    RenderOptions();
}

static void RenderOptions() {
    ApplyLanguage();

    // ── General (open by default): quick-access icon, language, Decoder Ring status ──
    // The real settings come first; file actions (backup, settings export) sit in their own
    // section at the very bottom, below a separator. Nexus draws the surrounding "Options"
    // header; addons cannot add sibling headers.
    if (ImGui::CollapsingHeader(Tr("General"), ImGuiTreeNodeFlags_DefaultOpen)) {
        // Quick-access icon toggle (persisted; default on).
        if (ImGui::Checkbox(Tr("Show quick access icon"), &g_Settings.showQaIcon)) {
            if (g_Settings.showQaIcon) AddQAIcon();
            else              RemoveQAIcon();
            SaveSettings();
        }

        // Language: Auto (= Nexus's language) or a fixed one. Language names stay native.
        const char* items[] = { Tr("Auto (Nexus language)"), "English", "Deutsch" };
        int mode = (int)g_Settings.language;
        if (ImGui::Combo(Tr("Language"), &mode, items, IM_ARRAYSIZE(items))) {
            g_Settings.language = (Trailpack::Settings::Language)mode;
            ApplyLanguage();
            SaveSettings();
        }

        // Live Decoder Ring presence — re-evaluated every frame, never latched. It is an
        // optional addon (full names + icons for chat-link chips), so its absence is a plain
        // grey info line, not a warning; "(?)" explains it.
        if (Trailpack::Decoder::Present()) {
            ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.50f, 1.0f), "%s", Tr("Decoder Ring: connected"));
        } else {
            ImGui::TextDisabled("%s", Tr("Chat-link chips: basic labels (optional: Decoder Ring addon)"));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Decoder Ring is an optional addon. With it, chat-link chips "
                                       "([&...]) show the real name, icon and a detailed tooltip; "
                                       "without it they show basic labels like [Item].\n"
                                       "Everything else works either way: copy code, open in wiki "
                                       "and event chips."));
    }

    // ── Font options ──────────────────────────────────────────────────────
    // Text sizes (live; saved when a slider is released): the window's UI elements,
    // and -- independently -- the note content (view, editor, preview).
    if (ImGui::CollapsingHeader(Tr("Font options"))) {
        auto sizeSlider = [](const char* id, const char* label, int& pct) {
            ImGui::PushID(id);
            ImGui::SliderInt(label, &pct, Trailpack::Settings::kScaleMin, Trailpack::Settings::kScaleMax, "%d%%");
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

    // ── Layout options ────────────────────────────────────────────────────
    // Pixel values (corner rounding, list row spacing): slider plus a number field next to it,
    // since even small values (e.g. 0.5 px) are visible. Saved when either is released.
    if (ImGui::CollapsingHeader(Tr("Layout options"))) {
        const float numW   = ImGui::CalcTextSize("00.00").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float resetW = NotesUi::ButtonWidth(Tr("Reset"), 0.0f);
        const float labelW = std::max(ImGui::CalcTextSize(Tr("Corner rounding")).x,
                                      ImGui::CalcTextSize(Tr("List row spacing")).x);
        const float sp     = ImGui::GetStyle().ItemSpacing.x;
        float sliderW = ImGui::GetContentRegionAvail().x - numW - resetW - labelW - 3.0f * sp;
        if (sliderW < 80.0f) sliderW = 80.0f;
        auto pxRow = [&](const char* id, const char* text, float& v, float maxV, float def) {
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderFloat("##slider", &v, 0.0f, maxV, "%.1f px");
            bool save = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(numW);
            ImGui::InputFloat("##value", &v, 0.0f, 0.0f, "%.2f");
            save = save || ImGui::IsItemDeactivatedAfterEdit();
            v = v < 0.0f ? 0.0f : v > maxV ? maxV : v;
            ImGui::SameLine();
            if (ImGui::Button(Tr("Reset")) && v != def) {
                v    = def;
                save = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(text);
            ImGui::PopID();
            return save;
        };
        bool save = pxRow("corner_rounding", Tr("Corner rounding"), g_Settings.cornerRounding,
                          Trailpack::Settings::kRoundingMax, 0.0f);
        // Space below each row of the note list (applied live while dragging).
        save = pxRow("row_spacing", Tr("List row spacing"), g_Settings.rowSpacing,
                     Trailpack::Settings::kRowSpacingMax, Trailpack::Settings::kRowSpacingDefault) || save;
        g_Notes.SetRowSpacing(g_Settings.rowSpacing);
        if (save) SaveSettings();

        // Note list order: ungrouped notes above the groups (default) or below them.
        if (ImGui::Checkbox(Tr("Groups above single notes"), &g_Settings.groupsFirst)) {
            g_Notes.SetGroupsFirst(g_Settings.groupsFirst);
            SaveSettings();
        }
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
        for (const char* l : { Tr("Theme"), Tr("Save as theme"), Tr("Border"), Tr("Background"), Tr("Text"),
                               Tr("Group names"), Tr("Menu button"), Tr("Menu icon") })
            labelW = std::max(labelW, ImGui::CalcTextSize(l).x);
        for (Trailpack::NoteType t : { Trailpack::NoteType::Daily, Trailpack::NoteType::Weekly })
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
        // Collapsible subsection (closed by default; ImGui remembers the state). Its content is
        // indented like Indent(); call ImGui::TreePop() after it when this returns true.
        auto section = [](const char* text, const char* id) {
            ImGui::Spacing();
            const std::string label = std::string(text) + "###" + id;   // id stays across languages
            return ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        };

        // ── Theme: built-ins and the user's saved themes ─────────────────
        // The combo shows the theme matching the current colours, else "Custom". Picking
        // one replaces every colour option; single colours can still be changed afterwards.
        {
            const Trailpack::ColorSet current = CollectColors();
            struct Builtin { const char* name; Trailpack::ColorSet colors; };
            const Builtin builtins[] = {
                { Tr("Default (Nexus)"), Trailpack::ColorSet()        },
                { Tr("Light"),           Trailpack::ColorSet::Light() },
                { Tr("Dark"),            Trailpack::ColorSet::Dark()  },
            };
            std::string preview = Tr("Custom");
            std::string currentUser;   // name of the matching user theme, if any
            for (const auto& t : g_Settings.themes)
                if (t.colors == current) { currentUser = preview = t.name; break; }
            if (currentUser.empty())
                for (const auto& b : builtins)
                    if (b.colors == current) { preview = b.name; break; }

            const float fieldW = ImGui::GetFontSize() * 12.0f;
            Trailpack::ColorSet pick;
            bool picked = false;
            ImGui::Indent();
            label(Tr("Theme"));
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::BeginCombo("##theme", preview.c_str())) {
                for (const auto& b : builtins)
                    if (ImGui::Selectable(b.name, currentUser.empty() && b.colors == current)) {
                        pick = b.colors; picked = true;
                    }
                if (!g_Settings.themes.empty()) ImGui::Separator();
                for (size_t i = 0; i < g_Settings.themes.size(); ++i) {
                    const Trailpack::NamedTheme& t = g_Settings.themes[i];
                    ImGui::PushID((int)i);   // a user theme may share a built-in's name
                    if (ImGui::Selectable(t.name.c_str(), t.name == currentUser)) {
                        pick = t.colors; picked = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (picked) { ApplyColors(pick); s_ColorsDirty = true; }
            if (!currentUser.empty()) {
                ImGui::SameLine();
                if (ImGui::Button(Tr("Delete theme"))) {
                    g_Settings.DeleteTheme(currentUser);
                    s_ColorsDirty = true;
                }
            }

            // Save the current colours under a name (same name: overwritten).
            static std::string s_ThemeName;
            label(Tr("Save as theme"));
            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputTextWithHint("##theme_name", Tr("Name"), &s_ThemeName);
            ImGui::SameLine();
            const bool exists = g_Settings.FindTheme(s_ThemeName) != nullptr;
            if (ImGui::Button(exists ? Tr("Overwrite") : Tr("Save")) &&
                g_Settings.SaveTheme(s_ThemeName, current)) {
                s_ThemeName.clear();
                s_ColorsDirty = true;
            }
            ImGui::Unindent();
        }

        // Same label in four sections: the id is scoped per section.
        auto borderToggle = [&](const char* id, bool& show) {
            ImGui::PushID(id);
            if (ImGui::Checkbox(Tr("Show border"), &show)) s_ColorsDirty = true;
            ImGui::PopID();
        };
        if (section(Tr("Window"), "sec_window")) {
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
            // Menu button next to "Create New Note" (opens Import / Export / Settings...).
            optionalRow("menu_button", Tr("Menu button"), c.hasMenuButton, c.menuButton, style.Colors[ImGuiCol_Button]);
            optionalRow("menu_icon",   Tr("Menu icon"),   c.hasMenuIcon,   c.menuIcon,   style.Colors[ImGuiCol_Text]);
            ImGui::TreePop();
        }

        // List / note panes: Nexus's child background and the window text until picked.
        const ImVec4 paneBg   = style.Colors[ImGuiCol_ChildBg];
        const ImVec4 paneText = style.Colors[ImGuiCol_Text];

        if (section(Tr("Note list"), "sec_list")) {
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
            ImGui::TreePop();
        }

        if (section(Tr("Note"), "sec_note")) {
            borderToggle("note_border_on", c.showNoteBorder);
            optionalRow("note_border", Tr("Border"),   c.hasNoteBorder, c.noteBorder, c.border);
            optionalRow("note_bg",   Tr("Background"), c.hasNoteBg,   c.noteBg,   paneBg);
            optionalRow("note_text", Tr("Text"),       c.hasNoteText, c.noteText, paneText);
            ImGui::TreePop();
        }

        // The editor field in edit mode (its background / text follow the note).
        if (section(Tr("Note editor"), "sec_editor")) {
            borderToggle("editor_border_on", c.showEditorBorder);
            optionalRow("editor_border", Tr("Border"), c.hasEditorBorder, c.editorBorder, c.border);
            ImGui::TreePop();
        }

        // Popups of the note window (menus, tooltips, the event picker, dialogs): until picked
        // they follow the note list (its colour, opaque, and its text) so they match the look.
        if (section(Tr("Popups"), "sec_popups")) {
            optionalRow("popup_bg",   Tr("Background"), c.hasPopupBg,   c.popupBg,   Theme::DefaultPopupBg());
            optionalRow("popup_text", Tr("Text"),       c.hasPopupText, c.popupText, Theme::DefaultPopupText());
            ImGui::TreePop();
        }

        // Daily / weekly badges: preview + name | background | letter | Reset (both).
        if (section(Tr("Daily / weekly badges"), "sec_badges")) {
            ImGui::SetCursorPosX(colSwatch); ImGui::TextDisabled("%s", Tr("Background"));
            ImGui::SameLine(colText);   ImGui::TextDisabled("%s", Tr("Text"));
            auto badgeRow = [&](Trailpack::NoteType t, ImVec4& bgCol, ImVec4& textCol, const ImVec4& bgDef) {
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
            badgeRow(Trailpack::NoteType::Daily,  c.dailyBadge,  c.dailyBadgeText,  Theme::BADGE_DAILY);
            badgeRow(Trailpack::NoteType::Weekly, c.weeklyBadge, c.weeklyBadgeText, Theme::BADGE_WEEKLY);
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::Button(Tr("Reset all colours"))) {
            ApplyColors(Trailpack::ColorSet());
            s_ColorsDirty = true;
        }
    }

    if (s_ColorsDirty && !ImGui::IsAnyItemActive()) {
        SaveSettings();
        s_ColorsDirty = false;
    }

    // ── Events ────────────────────────────────────────────────────────────
    // Event chips in checked tasks are grey names by default; these bring back parts of them.
    if (ImGui::CollapsingHeader(Tr("Events"))) {
        if (ImGui::Checkbox(Tr("Show the time in checked tasks"), &g_Settings.doneShowTime)) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Event chips in checked tasks keep their countdown, "
                                       "in a much lighter colour."));
        if (ImGui::Checkbox(Tr("Show tooltip and menu in checked tasks"), &g_Settings.doneShowPopups)) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Event chips in checked tasks keep their tooltip and right-click "
                                       "menu (wiki, copy waypoint). Left-click does nothing."));
    }

    // ── Data: backup and transfer ─────────────────────────────────────────
    // File actions, kept apart from the settings above: a backup (all notes, groups and
    // settings incl. themes; restoring replaces them) and the settings-only export / import
    // (e.g. to share a look or move it to another PC; notes stay). Both confirm before
    // replacing anything.
    PollBackupDialog();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(Tr("Data: backup and transfer"))) {
        ImGui::TextUnformatted(Tr("Backup"));
        ImGui::Separator();
        ImGui::TextWrapped("%s", Tr("A backup saves all notes, groups and settings (including themes) in one file."));
        if (ImGui::Button(Tr("Create backup..."))) StartFileDialog(FileJob::CreateBackup);
        ImGui::SameLine();
        if (ImGui::Button(Tr("Restore backup..."))) StartFileDialog(FileJob::RestoreBackup);

        if (!s_RestoreFile.empty()) {
            char line[512];
            snprintf(line, sizeof line,
                     Trailpack::I18n::TrN(s_RestoreNoteCount,
                                       "Restore %s (%d note)? This replaces all notes and settings.",
                                       "Restore %s (%d notes)? This replaces all notes and settings."),
                     s_RestoreFile.c_str(), s_RestoreNoteCount);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();
            Theme::PushGreenButton();
            if (ImGui::Button(Tr("Restore"))) RestoreBackup();
            Theme::PopButton();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Cancel"))) s_RestoreFile.clear();
        }
        if (!s_BackupMessage.empty()) ImGui::TextWrapped("%s", s_BackupMessage.c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted(Tr("Settings"));
        ImGui::Separator();
        ImGui::TextWrapped("%s", Tr("Saves all settings (including themes) without notes. "
                                    "Importing replaces all settings; the notes stay as they are. "
                                    "A backup file can be imported too (only its settings)."));
        if (ImGui::Button(Tr("Export settings..."))) StartFileDialog(FileJob::ExportSettings);
        ImGui::SameLine();
        if (ImGui::Button(Tr("Import settings..."))) StartFileDialog(FileJob::ImportSettings);

        if (!s_ImportSettingsFile.empty()) {
            char line[512];
            snprintf(line, sizeof line, Tr("Import settings from %s? This replaces all settings."),
                     s_ImportSettingsFile.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();
            ImGui::PushID("import_settings");
            Theme::PushGreenButton();
            if (ImGui::Button(Tr("Import"))) ImportSettings();
            Theme::PopButton();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Cancel"))) s_ImportSettingsFile.clear();
            ImGui::PopID();
        }
        if (!s_SettingsMessage.empty()) ImGui::TextWrapped("%s", s_SettingsMessage.c_str());
    }
}

// ── ProcessKeybind ────────────────────────────────────────────────────────
void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, KB_TOGGLE) == 0) {
        g_WindowVisible = !g_WindowVisible;
    }
}
