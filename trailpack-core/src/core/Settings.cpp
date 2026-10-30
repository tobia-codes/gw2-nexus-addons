// Settings.cpp — see Settings.h.
#include "core/Settings.h"

#include <nlohmann/json.hpp>

namespace Trailpack { namespace Core {

Settings Settings::FromJson(const std::string& text)
{
    Settings s;
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return s;
    auto it = j.find("offline");
    if (it != j.end() && it->is_boolean()) s.offline = it->get<bool>();
    return s;
}

std::string Settings::ToJson() const
{
    return nlohmann::json{{"format", "trailpack-core-settings"}, {"version", 1}, {"offline", offline}}.dump(2);
}

}} // namespace Trailpack::Core
