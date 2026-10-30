#pragma once
// Settings.h — PURE settings.json model of Core (addons/TrailpackCore/settings.json).

#include <string>

namespace Trailpack { namespace Core {

struct Settings {
    bool offline = false;   // "Offline only": no network, cache and built-in data only (D3)

    // Unknown / broken JSON gives the defaults.
    static Settings FromJson(const std::string& text);
    std::string ToJson() const;
};

}} // namespace Trailpack::Core
