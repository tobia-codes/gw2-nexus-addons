// Backup.cpp — see Backup.h.
#include "core/Backup.h"

#include <nlohmann/json.hpp>

namespace Trailpack { namespace Backup {

static const char* kFormat         = "trailpack-memo-backup";
static const int   kVersion        = 1;
static const char* kSettingsFormat = "trailpack-memo-settings";
static const int   kSettingsVersion = 1;

std::string Make(const std::string& notesJson, const std::string& settingsJson, int64_t created)
{
    nlohmann::json j;
    try {
        j["format"]   = kFormat;
        j["version"]  = kVersion;
        j["created"]  = created;
        j["notes"]    = nlohmann::json::parse(notesJson);
        j["settings"] = nlohmann::json::parse(settingsJson);
    } catch (...) {
        return std::string();
    }
    return j.dump(2);
}

bool Parse(const std::string& text, std::string& notesJson, std::string& settingsJson)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (...) {
        return false;
    }
    if (!j.is_object() || !j.contains("format") || j["format"] != kFormat) return false;
    if (!j.contains("version") || !j["version"].is_number_integer() ||
        j["version"].get<int>() < 1 || j["version"].get<int>() > kVersion)
        return false;
    if (!j.contains("notes") || !j.contains("settings") || !j["settings"].is_object())
        return false;
    notesJson    = j["notes"].dump();
    settingsJson = j["settings"].dump();
    return true;
}

std::string MakeSettings(const std::string& settingsJson, int64_t created)
{
    nlohmann::json j;
    try {
        j["format"]   = kSettingsFormat;
        j["version"]  = kSettingsVersion;
        j["created"]  = created;
        j["settings"] = nlohmann::json::parse(settingsJson);
    } catch (...) {
        return std::string();
    }
    if (!j["settings"].is_object()) return std::string();
    return j.dump(2);
}

bool ParseSettings(const std::string& text, std::string& settingsJson)
{
    std::string notes;
    if (Parse(text, notes, settingsJson)) return true;   // a full backup: its settings
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (...) {
        return false;
    }
    if (!j.is_object() || !j.contains("format") || j["format"] != kSettingsFormat) return false;
    if (!j.contains("version") || !j["version"].is_number_integer() ||
        j["version"].get<int>() < 1 || j["version"].get<int>() > kSettingsVersion)
        return false;
    if (!j.contains("settings") || !j["settings"].is_object()) return false;
    settingsJson = j["settings"].dump();
    return true;
}

}} // namespace Trailpack::Backup
