// Text.cpp — see Text.h. No <regex>: it is large in a static MinGW build.
#include "resolve/Text.h"

#include <cstring>

namespace Trailpack { namespace Core { namespace Text {

namespace {

bool StartsWithNoCase(const std::string& s, size_t at, const char* word)
{
    for (size_t i = 0; word[i] != '\0'; ++i) {
        if (at + i >= s.size()) return false;
        char c = s[at + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c != word[i]) return false;
    }
    return true;
}

// Replace "&name;" entities that occur in API / wiki text.
std::string DecodeEntities(const std::string& s)
{
    static const struct { const char* from; const char* to; } kEntities[] = {
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&#039;", "'"}, {"&apos;", "'"},
    };
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool hit = false;
        if (s[i] == '&') {
            for (const auto& e : kEntities) {
                const size_t n = std::strlen(e.from);
                if (s.compare(i, n, e.from) == 0) { out += e.to; i += n; hit = true; break; }
            }
        }
        if (!hit) out += s[i++];
    }
    return out;
}

std::string CollapseSpaces(const std::string& s)
{
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') { space = true; continue; }
        if (space && !out.empty() && c != '\n' && out.back() != '\n') out += ' ';
        space = false;
        out += c;
    }
    return out;
}

} // namespace

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string StripGameMarkup(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '<') {
            const size_t end = s.find('>', i);
            if (end == std::string::npos) { out += s.substr(i); break; }
            if (StartsWithNoCase(s, i, "<br")) out += '\n';
            i = end + 1;
            continue;
        }
        out += s[i++];
    }
    return Trim(DecodeEntities(out));
}

std::string StripWikiMarkup(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (StartsWithNoCase(s, i, "<sup")) {                    // coefficient notes
            const size_t end = s.find("</sup>", i);
            i = end == std::string::npos ? s.size() : end + 6;
            continue;
        }
        if (s[i] == '<') {
            const size_t end = s.find('>', i);
            i = end == std::string::npos ? s.size() : end + 1;
            continue;
        }
        if (s.compare(i, 2, "[[") == 0) {
            const size_t end = s.find("]]", i);
            if (end == std::string::npos) { i = s.size(); continue; }
            const std::string inner = s.substr(i + 2, end - i - 2);
            i = end + 2;
            if (StartsWithNoCase(inner, 0, "file:") || StartsWithNoCase(inner, 0, "category:")) continue;
            const size_t bar = inner.rfind('|');
            out += bar == std::string::npos ? inner : inner.substr(bar + 1);
            continue;
        }
        if (s.compare(i, 2, "''") == 0) {                        // '' italic / ''' bold
            while (i < s.size() && s[i] == '\'') ++i;
            continue;
        }
        out += s[i++];
    }
    out = Trim(CollapseSpaces(DecodeEntities(out)));
    size_t lead = 0;                                              // ":" bullet
    while (lead < out.size() && (out[lead] == ':' || out[lead] == ' ')) ++lead;
    return out.substr(lead);
}

std::string TruncateUtf8(const std::string& s, size_t maxBytes)
{
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // continuation byte
    return s.substr(0, cut);
}

}}} // namespace Trailpack::Core::Text
