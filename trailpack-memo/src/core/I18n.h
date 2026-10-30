#pragma once
// I18n.h — PURE (no ImGui/Nexus) UI translations for Trailpack Memo.
//
// The English text is the key: wrap every user-visible literal in Tr("...") and add its
// German version to the table in I18n.cpp. Missing translations fall back to English,
// so an untranslated string is never blank. Format strings must keep the same printf
// specifiers in both languages (host-tested).

#include <utility>
#include <vector>

namespace Trailpack { namespace I18n {

enum class Lang { En, De };

void SetLang(Lang lang);
Lang Current();

// The current language's text for English `en` (or `en` itself if untranslated).
// The returned pointer is static and stays valid.
const char* Tr(const char* en);

// Singular/plural pick: Tr(one) when n == 1, else Tr(many).
const char* TrN(long long n, const char* one, const char* many);

// The full English -> German table (for tests).
const std::vector<std::pair<const char*, const char*>>& GermanTable();

}} // namespace Trailpack::I18n
