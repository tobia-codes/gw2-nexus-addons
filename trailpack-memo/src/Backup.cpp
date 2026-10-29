// Backup.cpp — see Backup.h.
#include "Backup.h"

#include <nlohmann/json.hpp>

namespace Magpie { namespace Backup {

static const char* kFormat  = "trailpack-memo-backup";
static const int   kVersion = 1;

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

}} // namespace Magpie::Backup
