#include "chatlinks/ChatLinks.h"

namespace Trailpack { namespace ChatLinks {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Value of a base64 character, -1 for anything else (padding included).
int Sextet(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool IsBase64(char c) { return Sextet(c) >= 0 || c == '='; }

uint32_t ReadLE(const std::vector<uint8_t>& b, size_t at, int bytes)
{
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<uint32_t>(b[at + i]) << (8 * i);
    return v;
}

void WriteLE(std::vector<uint8_t>& b, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

std::string Wrap(const std::vector<uint8_t>& bytes)
{
    return "[&" + Base64Encode(bytes.data(), bytes.size()) + "]";
}

// Fixed part of a build link: type, profession, 3 x (spec, traits), 10 skills, 16 bytes.
constexpr size_t kBuildFixed = 1 + 1 + 6 + 20 + 16;

} // namespace

std::string Base64Encode(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16)
                         | (i + 1 < len ? uint32_t(data[i + 1]) << 8 : 0)
                         | (i + 2 < len ? uint32_t(data[i + 2]) : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < len ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < len ? kAlphabet[n & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const std::string& text)
{
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    size_t i = 0;
    for (; i < text.size() && text[i] != '='; ++i) {
        const int v = Sextet(text[i]);
        if (v < 0) return {};
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    for (; i < text.size(); ++i)            // only padding may follow
        if (text[i] != '=') return {};
    return out;
}

std::vector<uint8_t> Payload(const std::string& link)
{
    if (link.size() < 4 || link.compare(0, 2, "[&") != 0 || link.back() != ']') return {};
    return Base64Decode(link.substr(2, link.size() - 3));
}

LinkType TypeOf(const std::string& link)
{
    const auto b = Payload(link);
    return b.empty() ? LINK_NONE : static_cast<LinkType>(b[0]);
}

bool DecodeItem(const std::string& link, ItemLink& out)
{
    const auto b = Payload(link);
    if (b.size() < 6 || b[0] != LINK_ITEM) return false;
    out = ItemLink{};
    out.count  = b[1];
    out.itemId = ReadLE(b, 2, 3);
    const uint8_t flags = b[5];
    size_t at = 6;
    uint32_t* optional[] = {&out.skinId, &out.upgrade1, &out.upgrade2};
    const uint8_t bits[] = {ITEM_HAS_SKIN, ITEM_HAS_UPGRADE1, ITEM_HAS_UPGRADE2};
    for (int i = 0; i < 3; ++i) {
        if (!(flags & bits[i])) continue;
        if (at + 4 > b.size()) return false;
        *optional[i] = ReadLE(b, at, 3);   // 3-byte id in a 4-byte slot
        at += 4;
    }
    return true;
}

bool DecodeBuild(const std::string& link, BuildLink& out)
{
    const auto b = Payload(link);
    if (b.size() < kBuildFixed || b[0] != LINK_BUILD) return false;
    out = BuildLink{};
    out.profession = b[1];
    size_t at = 2;
    for (auto& s : out.specs) {
        s.id = b[at];
        const uint8_t t = b[at + 1];
        for (int k = 0; k < 3; ++k) s.traits[k] = (t >> (2 * k)) & 3;
        at += 2;
    }
    for (int k = 0; k < 5; ++k) {           // land / water skills alternate
        out.landSkills[k]  = static_cast<uint16_t>(ReadLE(b, at, 2));
        out.waterSkills[k] = static_cast<uint16_t>(ReadLE(b, at + 2, 2));
        at += 4;
    }
    for (auto& v : out.professionData) v = b[at++];

    // Optional extension: weapon types, then skill overrides. A truncated
    // extension keeps what was read; the fixed part is what matters.
    if (at < b.size()) {
        const size_t n = b[at++];
        for (size_t k = 0; k < n && at + 2 <= b.size(); ++k, at += 2)
            out.weapons.push_back(static_cast<uint16_t>(ReadLE(b, at, 2)));
    }
    if (at < b.size()) {
        const size_t n = b[at++];
        for (size_t k = 0; k < n && at + 4 <= b.size(); ++k, at += 4)
            out.skillOverrides.push_back(ReadLE(b, at, 4));
    }
    return true;
}

std::string EncodeItem(uint32_t itemId, int count, uint32_t skinId, uint32_t upgrade1, uint32_t upgrade2)
{
    if (count < 1) count = 1;
    if (count > 250) count = 250;
    uint8_t flags = 0;
    if (skinId)   flags |= ITEM_HAS_SKIN;
    if (upgrade1) flags |= ITEM_HAS_UPGRADE1;
    if (upgrade2) flags |= ITEM_HAS_UPGRADE2;
    std::vector<uint8_t> b{LINK_ITEM, static_cast<uint8_t>(count)};
    WriteLE(b, itemId & 0xFFFFFF, 3);
    b.push_back(flags);
    for (uint32_t v : {skinId, upgrade1, upgrade2})
        if (v) WriteLE(b, v, 4);
    return Wrap(b);
}

std::string EncodeId(LinkType type, uint32_t id)
{
    std::vector<uint8_t> b{type};
    WriteLE(b, id, 4);
    return Wrap(b);
}

std::string EncodeBuild(const BuildLink& build)
{
    std::vector<uint8_t> b{LINK_BUILD, build.profession};
    for (const auto& s : build.specs) {
        b.push_back(s.id);
        uint8_t t = 0;
        for (int k = 0; k < 3; ++k) t |= static_cast<uint8_t>((s.traits[k] & 3) << (2 * k));
        b.push_back(t);
    }
    for (int k = 0; k < 5; ++k) {
        WriteLE(b, build.landSkills[k], 2);
        WriteLE(b, build.waterSkills[k], 2);
    }
    b.insert(b.end(), build.professionData, build.professionData + 16);
    if (!build.weapons.empty() || !build.skillOverrides.empty()) {
        b.push_back(static_cast<uint8_t>(build.weapons.size()));
        for (uint16_t w : build.weapons) WriteLE(b, w, 2);
        b.push_back(static_cast<uint8_t>(build.skillOverrides.size()));
        for (uint32_t s : build.skillOverrides) WriteLE(b, s, 4);
    }
    return Wrap(b);
}

const char* TypeLabel(LinkType type)
{
    switch (type) {
        case LINK_COIN:          return "[Coin]";
        case LINK_ITEM:          return "[Item]";
        case LINK_NPC_TEXT:      return "[Text]";
        case LINK_MAP:           return "[Waypoint]";
        case LINK_PVP_GAME:      return "[PvP]";
        case LINK_SKILL:         return "[Skill]";
        case LINK_TRAIT:         return "[Trait]";
        case LINK_USER:          return "[User]";
        case LINK_RECIPE:        return "[Recipe]";
        case LINK_SKIN:          return "[Skin]";
        case LINK_OUTFIT:        return "[Outfit]";
        case LINK_WVW_OBJECTIVE: return "[WvW]";
        case LINK_BUILD:         return "[Build]";
        case LINK_ACHIEVEMENT:   return "[Achievement]";
        case LINK_WARDROBE:      return "[Wardrobe]";
        default:                 return "[Link]";
    }
}

const char* ProfessionName(uint8_t code)
{
    static const char* const kNames[] = {"", "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
                                         "Elementalist", "Mesmer", "Necromancer", "Revenant"};
    return code < sizeof(kNames) / sizeof(kNames[0]) ? kNames[code] : "";
}

std::vector<Segment> Split(const std::string& utf8)
{
    std::vector<Segment> out;
    size_t plainStart = 0;
    auto flush = [&](size_t end) {
        if (end <= plainStart) return;
        Segment s;
        s.raw = utf8.substr(plainStart, end - plainStart);
        out.push_back(std::move(s));
    };

    size_t i = 0;
    while (i + 1 < utf8.size()) {
        if (utf8[i] != '[' || utf8[i + 1] != '&') { ++i; continue; }
        size_t j = i + 2;
        while (j < utf8.size() && IsBase64(utf8[j])) ++j;
        if (j >= utf8.size() || utf8[j] != ']') { ++i; continue; }

        const std::string raw = utf8.substr(i, j + 1 - i);
        const auto b = Payload(raw);
        if (b.empty()) { ++i; continue; }

        Segment s;
        s.isLink = true;
        s.raw    = raw;
        s.type   = static_cast<LinkType>(b[0]);
        if (s.type == LINK_ITEM) {
            if (b.size() >= 2) s.count = b[1];
            if (b.size() >= 5) s.id = ReadLE(b, 2, 3);
        } else if (b.size() >= 5) {
            s.id = ReadLE(b, 1, 4);
            if (s.type == LINK_WVW_OBJECTIVE && b.size() >= 9) s.secondId = ReadLE(b, 5, 4);
        }
        flush(i);
        out.push_back(std::move(s));
        i = j + 1;
        plainStart = i;
    }
    flush(utf8.size());
    return out;
}

}} // namespace Trailpack::ChatLinks
