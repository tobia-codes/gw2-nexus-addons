// ============================================================================
// Trailpack Core — Nexus entry point (PLAN stage 4).
// ----------------------------------------------------------------------------
// Headless: no window, no keybind. Publishes the TpcApi table under the DataLink
// TRAILPACK_CORE_DATALINK, answers PING with READY, drives the Resolver on one worker
// thread (WinINet), raises EV_TRAILPACK_CORE_RESOLVED on the render thread and renders a
// settings page in Nexus' options. All logic lives in the pure units (api/Service, resolve/*,
// events/*); this file only binds them to Nexus.
// ============================================================================

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "imgui.h"
#include "nexus/Nexus.h"

#include "TrailpackCoreApi.h"
#include "api/Service.h"
#include "chatlinks/ChatLinks.h"
#include "core/I18n.h"
#include "core/Settings.h"
#include "events/EventSchedule.h"
#include "generated/GameData.h"
#include "net/HttpWinInet.h"
#include "tpfiles/SafeFile.h"
#include "tpui/Style.h"

#define V_MAJOR    0
#define V_MINOR    1
#define V_BUILD    0
#define V_REVISION 0

#define ADDON_NAME     "Trailpack Core"
#define ADDON_DIR      "TrailpackCore"
#define LOC_LANG_PROBE "TPC_LANG_PROBE"   // Nexus localization key whose value is the active language

namespace Core = Trailpack::Core;
namespace Ui   = Trailpack::Ui;
namespace CL   = Trailpack::ChatLinks;

// ── State ─────────────────────────────────────────────────────────────────
static AddonDefinition_t AddonDef{};
static AddonAPI_t*       APIDefs   = nullptr;
static TpcApi*           g_Api     = nullptr;   // the shared DataLink block (Nexus-owned)
static Core::Service*    g_Service = nullptr;
static Core::Settings    g_Settings;
static std::string       g_Dir;                 // addons/TrailpackCore

static std::thread             g_Worker;
static std::mutex              g_WakeMutex;
static std::condition_variable g_Wake;
static bool                    g_Kick = false;  // new work queued (guarded by g_WakeMutex)
static std::atomic<bool>       g_Stop{false};
static std::atomic<bool>       g_ClearCache{false};

static const uint8_t kCachedTypes[] = { CL::LINK_ITEM, CL::LINK_SKIN, CL::LINK_SKILL, CL::LINK_TRAIT, CL::LINK_RECIPE };

static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

static Core::Lang Lang() {
    return g_Service ? g_Service->GetLang() : Core::Lang::En;
}

static const char* Tr(const char* en) {
    return Core::I18n::Tr(en, Lang());
}

// Nexus' language through a probe key registered at load ("en" / "de"); any other language
// returns the key itself -> English (D12).
static Core::Lang NexusLang() {
    if (!APIDefs || !APIDefs->Localization_Translate) return Core::Lang::En;
    const char* code = APIDefs->Localization_Translate(LOC_LANG_PROBE);
    return (code && std::strcmp(code, "de") == 0) ? Core::Lang::De : Core::Lang::En;
}

// ── Files ─────────────────────────────────────────────────────────────────
static std::string SettingsPath() { return Trailpack::SafeFile::Join(g_Dir, "settings.json"); }

static std::string CachePath(Core::Lang lang, uint8_t type) {
    const char* name = "other.json";
    switch (type) {
        case CL::LINK_ITEM:   name = "items.json";   break;
        case CL::LINK_SKIN:   name = "skins.json";   break;
        case CL::LINK_SKILL:  name = "skills.json";  break;
        case CL::LINK_TRAIT:  name = "traits.json";  break;
        case CL::LINK_RECIPE: name = "recipes.json"; break;
    }
    const std::string dir = Trailpack::SafeFile::Join(Trailpack::SafeFile::Join(g_Dir, "cache"), Core::ApiCode(lang));
    return Trailpack::SafeFile::Join(dir, name);
}

static void SaveSettings() {
    if (!g_Dir.empty()) Trailpack::SafeFile::WriteAtomic(SettingsPath(), g_Settings.ToJson());
}

// Worker thread only.
static void LoadCaches() {
    for (Core::Lang lang : { Core::Lang::En, Core::Lang::De })
        for (uint8_t type : kCachedTypes) {
            std::string text;
            if (Trailpack::SafeFile::Read(CachePath(lang, type), text))
                g_Service->GetResolver().LoadCache(lang, type, text);
        }
}

// Worker thread only.
static void SaveDirtyCaches() {
    for (const auto& d : g_Service->GetResolver().TakeDirty()) {
        const std::string path = CachePath(d.first, d.second);
        Trailpack::SafeFile::CreateDirectories(Trailpack::SafeFile::ParentPath(path));
        Trailpack::SafeFile::WriteAtomic(path, g_Service->GetResolver().SaveCache(d.first, d.second));
    }
}

// ── Worker ────────────────────────────────────────────────────────────────
static void Kick() {
    { std::lock_guard<std::mutex> lock(g_WakeMutex); g_Kick = true; }
    g_Wake.notify_one();
}

static void WorkerMain() {
    LoadCaches();
    int64_t lastSave = NowMs();
    while (!g_Stop) {
        if (g_ClearCache.exchange(false)) g_Service->GetResolver().ClearCache();
        const int64_t now  = NowMs();
        const int64_t next = g_Service->GetResolver().Step(now);
        if (now - lastSave >= 10000) {                       // throttled disk writes (D9)
            SaveDirtyCaches();
            lastSave = now;
        }
        std::unique_lock<std::mutex> lock(g_WakeMutex);
        const int64_t wait = std::min<int64_t>(std::max<int64_t>(next - now, 0), 10000);
        if (wait > 0)
            g_Wake.wait_for(lock, std::chrono::milliseconds(wait), [] { return g_Kick || g_Stop.load(); });
        g_Kick = false;
    }
    SaveDirtyCaches();
}

// ── ABI functions (bound into the DataLink table) ─────────────────────────
static TpcStatus Api_Resolve(uint8_t type, uint32_t id, const char* chatCode, TpcRecord* out) {
    if (!g_Service) return TPC_FAILED;
    g_Service->SetNow(NowMs());
    const TpcStatus st = g_Service->Resolve(type, id, chatCode, out);
    if (st == TPC_NOT_READY) Kick();
    return st;
}

static uint32_t Api_ResolveMany(uint8_t type, const uint32_t* ids, uint32_t count, TpcRecord* out) {
    if (!g_Service) return 0;
    g_Service->SetNow(NowMs());
    const uint32_t warm = g_Service->ResolveMany(type, ids, count, out);
    if (warm < count) Kick();
    return warm;
}

static uint8_t Api_ActiveLanguage() {
    return Lang() == Core::Lang::De ? TPC_LANG_DE : TPC_LANG_EN;
}

static uint32_t Api_EventCount() { return g_Service ? g_Service->EventCount() : 0; }
static bool Api_EventByIndex(uint32_t i, uint8_t lang, TpcEvent* out) { return g_Service && g_Service->EventByIndex(i, lang, out); }
static bool Api_EventById(const char* id, uint8_t lang, TpcEvent* out) { return g_Service && g_Service->EventById(id, lang, out); }
static bool Api_EventState(const char* id, int64_t now, TpcOccurrence* out) { return g_Service && g_Service->EventState(id, now, out); }
static uint32_t Api_EventNext(const char* id, int64_t now, TpcOccurrence* out, uint32_t max) {
    return g_Service ? g_Service->EventNext(id, now, out, max) : 0;
}
static uint32_t Api_EventSearch(const char* query, TpcEventId* out, uint32_t max) {
    return g_Service ? g_Service->EventSearch(query, out, max) : 0;
}

static void PublishApi() {
    g_Api = (TpcApi*)APIDefs->DataLink_Share(TRAILPACK_CORE_DATALINK, sizeof(TpcApi));
    if (!g_Api) return;
    std::memset(g_Api, 0, sizeof(TpcApi));
    g_Api->structSize     = sizeof(TpcApi);
    g_Api->Resolve        = Api_Resolve;
    g_Api->ResolveMany    = Api_ResolveMany;
    g_Api->ActiveLanguage = Api_ActiveLanguage;
    g_Api->EventCount     = Api_EventCount;
    g_Api->EventByIndex   = Api_EventByIndex;
    g_Api->EventById      = Api_EventById;
    g_Api->EventState     = Api_EventState;
    g_Api->EventNext      = Api_EventNext;
    g_Api->EventSearch    = Api_EventSearch;
    g_Api->apiVersion     = TRAILPACK_CORE_API_VERSION;   // last: the table is complete now
}

// Zero every pointer before UNLOADING fires: a consumer that still calls one sees null.
static void RetractApi() {
    if (!g_Api) return;
    g_Api->apiVersion = 0;
    std::memset(((char*)g_Api) + sizeof(uint32_t), 0, sizeof(TpcApi) - sizeof(uint32_t));
    g_Api = nullptr;
}

static void OnPing(void*) {
    if (APIDefs && g_Api) APIDefs->Events_Raise(EV_TRAILPACK_CORE_READY, nullptr);
}

// ── Render thread ─────────────────────────────────────────────────────────
// Language follow-up and the RESOLVED events (payload valid only during the call).
static void AddonPreRender() {
    if (!g_Service) return;
    g_Service->SetLang(NexusLang());
    g_Service->SetNow(NowMs());
    for (auto& rec : g_Service->TakeResolved())
        APIDefs->Events_Raise(EV_TRAILPACK_CORE_RESOLVED, &rec);
}

static void AddonOptions() {
    if (!g_Service) return;
    const Core::ResolverStats st = g_Service->GetResolver().Stats(NowMs());

    if (ImGui::CollapsingHeader(Tr("General"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (st.offline)                   ImGui::TextColored(Ui::STATUS_WARN, "%s", Tr("Offline only"));
        else if (!st.lastError.empty())   ImGui::TextColored(Ui::STATUS_ERROR, Tr("Last error: %s"), st.lastError.c_str());
        else                              ImGui::TextColored(Ui::STATUS_OK, "%s", Tr("Online"));

        ImGui::Text(Tr("Language: %s"), Lang() == Core::Lang::De ? "Deutsch" : "English");
        Ui::HelpMarker(Tr("Follows the Nexus language; other languages use English."));
        if (st.gameBuild) ImGui::Text(Tr("Game build: %u"), st.gameBuild);
        else              ImGui::TextDisabled("%s", Tr("Game build: not known yet"));
        ImGui::Text(Tr("Requests in the last minute: %d"), st.requestsLastMinute);
        ImGui::Text(Tr("Waiting for data: %d"), (int)st.queued);

        if (ImGui::Checkbox(Tr("Offline only"), &g_Settings.offline)) {
            g_Service->GetResolver().SetOffline(g_Settings.offline);
            SaveSettings();
            Kick();
        }
        Ui::HelpMarker(Tr("No network requests: only cached and built-in data. Waypoints, builds and events always work."));
    }

    if (ImGui::CollapsingHeader(Tr("Cache"))) {
        static bool s_Confirm = false;
        ImGui::Text(Tr("Cached entries: %d English, %d German"), (int)st.cached[0], (int)st.cached[1]);
        Ui::HelpMarker(Tr("Names, icons and tooltips from the GW2 API are kept here so they show instantly next time. "
                          "After clearing, Core fetches them again when needed."));
        if (!s_Confirm) {
            Ui::PushAmberButton();
            if (ImGui::Button(Tr("Clear cache"))) s_Confirm = true;
            Ui::PopButton();
        } else {
            ImGui::TextUnformatted(Tr("Clear the cache?"));
            ImGui::SameLine();
            Ui::PushGreenButton();
            if (ImGui::Button(Tr("Yes"))) {
                g_ClearCache = true;
                s_Confirm = false;
                Kick();
            }
            Ui::PopButton();
            ImGui::SameLine();
            if (ImGui::Button(Tr("No"))) s_Confirm = false;
        }
    }

    if (ImGui::CollapsingHeader(Tr("About"))) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(Tr("Trailpack Core provides game data for all Trailpack addons. It only talks to the official "
                                  "GW2 API and the GW2 wiki, never touches the game and needs no API key."));
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled(Tr("Built-in data: %d events, %d waypoints and points of interest."),
                            (int)Core::Events::kEventCount, (int)Core::GameData::kPoiCount);
    }
}

// ── Load / unload ─────────────────────────────────────────────────────────
static void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc,
                                 (void (*)(void*, void*))APIDefs->ImguiFree);

    APIDefs->Localization_Set(LOC_LANG_PROBE, "en", "en");
    APIDefs->Localization_Set(LOC_LANG_PROBE, "de", "de");

    if (const char* dir = APIDefs->Paths_GetAddonDirectory(ADDON_DIR)) g_Dir = dir;
    if (!g_Dir.empty()) {
        Trailpack::SafeFile::CreateDirectories(g_Dir);
        std::string text;
        if (Trailpack::SafeFile::Read(SettingsPath(), text)) g_Settings = Core::Settings::FromJson(text);
    }

    char ua[160];
    std::snprintf(ua, sizeof ua, "TrailpackCore/%d.%d.%d (+https://github.com/tobia-codes/gw2-nexus-addons)",
                  V_MAJOR, V_MINOR, V_BUILD);
    Core::Http::SetUserAgent(ua);

    g_Service = new Core::Service(Core::Http::Get);
    g_Service->SetLang(NexusLang());
    g_Service->SetNow(NowMs());
    g_Service->GetResolver().SetOffline(g_Settings.offline);

    g_Stop   = false;
    g_Worker = std::thread(WorkerMain);

    APIDefs->GUI_Register(RT_PreRender, AddonPreRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    PublishApi();
    APIDefs->Events_Subscribe(EV_TRAILPACK_CORE_PING, OnPing);
    APIDefs->Events_Raise(EV_TRAILPACK_CORE_READY, nullptr);
    APIDefs->Log(LOGL_INFO, "TrailpackCore", "Trailpack Core loaded.");
}

static void AddonUnload() {
    // 1. Consumers first: zero the table, then tell them (D7).
    RetractApi();
    APIDefs->Events_Raise(EV_TRAILPACK_CORE_UNLOADING, nullptr);
    APIDefs->Events_Unsubscribe(EV_TRAILPACK_CORE_PING, OnPing);
    APIDefs->GUI_Deregister(AddonPreRender);
    APIDefs->GUI_Deregister(AddonOptions);

    // 2. Stop the worker (waits for a request in flight), then write the caches.
    g_Stop = true;
    g_Service->GetResolver().Shutdown();
    Kick();
    if (g_Worker.joinable()) g_Worker.join();

    delete g_Service;
    g_Service = nullptr;
    APIDefs = nullptr;
}

// ── GetAddonDef (exported) ────────────────────────────────────────────────
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature        = 0x5450436F;          // "TPCo"
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = ADDON_NAME;
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author           = "tobia-codes";
    AddonDef.Description      = "Game data for all Trailpack addons: chat-link names, icons and tooltips, events and waypoints.";
    AddonDef.Load             = AddonLoad;
    AddonDef.Unload           = AddonUnload;
    AddonDef.Flags            = AF_None;
    AddonDef.Provider         = UP_GitHub;                                    // releases: see .github/workflows/release.yml
    AddonDef.UpdateLink       = "https://github.com/tobia-codes/trailpack-core";
    return &AddonDef;
}
