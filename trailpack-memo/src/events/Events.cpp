// Events.cpp — see Events.h.
#include "events/Events.h"

#include <cstdio>

namespace Trailpack { namespace Events {

using I18n::Tr;

static const char kOpen[]  = "{{event:";
static const char kClose[] = "}}";

std::string Token(const std::string& id)
{
    return kOpen + id + kClose;
}

size_t FindToken(const std::string& s, size_t from, size_t& length, std::string& id)
{
    const size_t openLen = sizeof kOpen - 1;
    for (size_t at = s.find(kOpen, from); at != std::string::npos; at = s.find(kOpen, at + 1)) {
        size_t i = at + openLen;
        while (i < s.size() && i - (at + openLen) < 64 &&
               ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-'))
            ++i;
        if (i > at + openLen && s.compare(i, 2, kClose) == 0) {
            id = s.substr(at + openLen, i - (at + openLen));
            length = i + 2 - at;
            return at;
        }
    }
    return std::string::npos;
}

bool IsToken(const std::string& s, std::string* id)
{
    size_t len = 0;
    std::string found;
    if (FindToken(s, 0, len, found) != 0 || len != s.size()) return false;
    if (id != nullptr) *id = found;
    return true;
}

static void appendUtf8(std::string& s, unsigned int cp)
{
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

Trigger FindTrigger(const std::vector<unsigned int>& text, int caret)
{
    Trigger t;
    if (caret < 0 || caret > (int)text.size()) return t;
    // Walk back from the caret over the query (stops at a line break, a chip or the limit);
    // every '/' on the way is a candidate, the nearest one to the caret wins.
    for (int slash = caret - 1; slash >= 0 && caret - slash <= kMaxTriggerQuery + 7; --slash) {
        const unsigned int c = text[slash];
        if (c == '\n' || c == 0xFFFCu) break;
        if (c != '/') continue;
        if (slash > 0 && text[slash - 1] != ' ' && text[slash - 1] != '\n') continue;
        // "e " or "event " after the slash, any case.
        int q = -1;
        for (const char* word : { "e ", "event " }) {
            int i = slash + 1;
            const char* w = word;
            while (*w != '\0' && i < caret &&
                   (text[i] == (unsigned char)*w || text[i] == (unsigned char)*w - 32u)) {
                ++w;
                ++i;
            }
            if (*w == '\0') { q = i; break; }
        }
        if (q < 0 || caret - q > kMaxTriggerQuery) continue;
        t.found = true;
        t.start = slash;
        for (int i = q; i < caret; ++i) appendUtf8(t.query, text[i]);
        return t;
    }
    return t;
}

std::string PrettyId(const std::string& id)
{
    std::string out = id;
    for (char& c : out) if (c == '-') c = ' ';
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
    return out;
}

std::string FormatTime(int64_t seconds, bool active)
{
    if (seconds < 0) seconds = 0;
    char buf[64];
    const int s = (int)(seconds % 60);
    const int m = (int)((seconds / 60) % 60);
    const int h = (int)(seconds / 3600);
    if (seconds < 60) {
        snprintf(buf, sizeof buf, active ? Tr("for %d s") : Tr("in %d s"), s);
    } else if (h == 0) {
        snprintf(buf, sizeof buf, active ? Tr("for %d min") : Tr("in %d min"), m);
    } else if (m == 0) {
        snprintf(buf, sizeof buf, active ? Tr("for %d h") : Tr("in %d h"), h);
    } else {
        snprintf(buf, sizeof buf, active ? Tr("for %d h %d min") : Tr("in %d h %d min"), h, m);
    }
    return buf;
}

std::string FormatDuration(int64_t seconds)
{
    if (seconds < 0) seconds = 0;
    char buf[64];
    const int m = (int)((seconds / 60) % 60);
    const int h = (int)(seconds / 3600);
    if (h == 0)      snprintf(buf, sizeof buf, Tr("%d min"), m);
    else if (m == 0) snprintf(buf, sizeof buf, Tr("%d h"), h);
    else             snprintf(buf, sizeof buf, Tr("%d h %d min"), h, m);
    return buf;
}

}} // namespace Trailpack::Events
