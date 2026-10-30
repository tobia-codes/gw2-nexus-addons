#pragma once
// Lang.h — the two languages Core serves (PLAN D12). PURE.

#include <cstdint>

namespace Trailpack { namespace Core {

enum class Lang : uint8_t { En = 0, De = 1 };

// API query value ("en" / "de").
inline const char* ApiCode(Lang lang) { return lang == Lang::De ? "de" : "en"; }

}} // namespace Trailpack::Core
