#pragma once
// I18n.h — PURE English -> German texts of Core's settings page. The English text is the key;
// a missing translation falls back to English. Same printf specifiers in both languages.

#include <utility>
#include <vector>

#include "core/Lang.h"

namespace Trailpack { namespace Core { namespace I18n {

const char* Tr(const char* en, Lang lang);

// The full table (for tests).
const std::vector<std::pair<const char*, const char*>>& GermanTable();

}}} // namespace Trailpack::Core::I18n
