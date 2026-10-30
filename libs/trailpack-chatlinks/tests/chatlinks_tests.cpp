// Host tests for libs/trailpack-chatlinks.
#include "chatlinks/ChatLinks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int s_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n"; \
            ++s_failures; \
        } \
    } while (false)

using namespace Trailpack::ChatLinks;

static void test_base64_roundtrip()
{
    for (size_t len = 0; len < 12; ++len) {
        std::vector<uint8_t> in;
        for (size_t i = 0; i < len; ++i) in.push_back(static_cast<uint8_t>(i * 37 + 5));
        const std::string enc = Base64Encode(in.data(), in.size());
        CHECK(enc.size() % 4 == 0);
        CHECK(Base64Decode(enc) == in);
    }
    CHECK(Base64Encode(reinterpret_cast<const uint8_t*>("Man"), 3) == "TWFu");
    CHECK(Base64Decode("TWE=") == (std::vector<uint8_t>{'M', 'a'}));
    CHECK(Base64Decode("TW#u").empty());     // invalid character
    CHECK(Base64Decode("TW=u").empty());     // data after padding
}

static void test_real_item_link()
{
    // [&AgH1WgAA]: 1 x item 23285 (the example from the GW2 wiki "Chat link format").
    ItemLink it;
    CHECK(DecodeItem("[&AgH1WgAA]", it));
    CHECK(it.itemId == 23285u);
    CHECK(it.count == 1);
    CHECK(it.skinId == 0u && it.upgrade1 == 0u && it.upgrade2 == 0u);
    CHECK(EncodeItem(23285) == "[&AgH1WgAA]");
    CHECK(TypeOf("[&AgH1WgAA]") == LINK_ITEM);
}

static void test_item_with_skin_and_upgrades()
{
    const std::string link = EncodeItem(46774, 3, 5807, 24615, 24618);
    ItemLink it;
    CHECK(DecodeItem(link, it));
    CHECK(it.itemId == 46774u);
    CHECK(it.count == 3);
    CHECK(it.skinId == 5807u);
    CHECK(it.upgrade1 == 24615u);
    CHECK(it.upgrade2 == 24618u);

    // Only the second upgrade slot set: the reader must not shift it into slot 1.
    CHECK(DecodeItem(EncodeItem(46774, 1, 0, 0, 24618), it));
    CHECK(it.upgrade1 == 0u && it.upgrade2 == 24618u);
}

static void test_item_count_clamped()
{
    ItemLink it;
    CHECK(DecodeItem(EncodeItem(1, 999), it) && it.count == 250);
    CHECK(DecodeItem(EncodeItem(1, 0), it) && it.count == 1);
}

static void test_item_truncated_is_rejected()
{
    // Flags announce a skin but the payload ends: not a valid item link.
    const uint8_t bytes[] = {LINK_ITEM, 1, 0xF5, 0x5A, 0x00, ITEM_HAS_SKIN};
    ItemLink it;
    CHECK(!DecodeItem("[&" + Base64Encode(bytes, sizeof(bytes)) + "]", it));
    CHECK(!DecodeItem(EncodeId(LINK_SKILL, 5516), it));   // wrong type
}

static void test_id_links()
{
    const LinkType types[] = {LINK_SKILL, LINK_TRAIT, LINK_SKIN, LINK_RECIPE, LINK_MAP,
                              LINK_OUTFIT, LINK_ACHIEVEMENT};
    for (LinkType t : types) {
        const std::string link = EncodeId(t, 123456);
        CHECK(TypeOf(link) == t);
        const auto segs = Split(link);
        CHECK(segs.size() == 1 && segs[0].isLink && segs[0].type == t && segs[0].id == 123456u);
    }
    // Waypoint example from the GW2 wiki: [&BDAEAAA=] is PoI 1072.
    const auto segs = Split("[&BDAEAAA=]");
    CHECK(segs.size() == 1 && segs[0].type == LINK_MAP && segs[0].id == 1072u);
    CHECK(EncodeId(LINK_MAP, 1072) == "[&BDAEAAA=]");
}

static void test_build_roundtrip()
{
    BuildLink b;
    b.profession = PROF_MESMER;
    b.specs[0].id = 10; b.specs[0].traits[0] = 1; b.specs[0].traits[1] = 2; b.specs[0].traits[2] = 3;
    b.specs[1].id = 1;
    b.specs[2].id = 59;  b.specs[2].traits[2] = 2;
    for (int k = 0; k < 5; ++k) { b.landSkills[k] = static_cast<uint16_t>(100 + k); b.waterSkills[k] = static_cast<uint16_t>(200 + k); }
    b.professionData[0] = 7; b.professionData[15] = 9;
    b.weapons = {1, 2, 3};
    b.skillOverrides = {70000, 70001};

    const std::string link = EncodeBuild(b);
    CHECK(TypeOf(link) == LINK_BUILD);
    BuildLink d;
    CHECK(DecodeBuild(link, d));
    CHECK(d.profession == PROF_MESMER);
    CHECK(d.specs[0].id == 10 && d.specs[0].traits[0] == 1 && d.specs[0].traits[1] == 2 && d.specs[0].traits[2] == 3);
    CHECK(d.specs[2].id == 59 && d.specs[2].traits[2] == 2);
    CHECK(d.landSkills[4] == 104 && d.waterSkills[0] == 200);
    CHECK(d.professionData[0] == 7 && d.professionData[15] == 9);
    CHECK(d.weapons == (std::vector<uint16_t>{1, 2, 3}));
    CHECK(d.skillOverrides == (std::vector<uint32_t>{70000, 70001}));
    CHECK(EncodeBuild(d) == link);

    // Without the extension the link is exactly the fixed 44 bytes.
    BuildLink plain;
    plain.profession = PROF_GUARDIAN;
    CHECK(Payload(EncodeBuild(plain)).size() == 44u);
    CHECK(DecodeBuild(EncodeBuild(plain), d) && d.weapons.empty() && d.skillOverrides.empty());
    CHECK(!DecodeBuild(EncodeId(LINK_BUILD, 1), d));   // too short
}

static void test_labels()
{
    CHECK(std::string(TypeLabel(LINK_ITEM)) == "[Item]");
    CHECK(std::string(TypeLabel(LINK_MAP)) == "[Waypoint]");
    CHECK(std::string(TypeLabel(LINK_SKILL)) == "[Skill]");
    CHECK(std::string(TypeLabel(LINK_SKIN)) == "[Skin]");
    CHECK(std::string(TypeLabel(LINK_NONE)) == "[Link]");
    CHECK(std::string(ProfessionName(PROF_MESMER)) == "Mesmer");
    CHECK(std::string(ProfessionName(PROF_REVENANT)) == "Revenant");
    CHECK(std::string(ProfessionName(0)).empty());
    CHECK(std::string(ProfessionName(99)).empty());
}

static std::string Join(const std::vector<Segment>& segs)
{
    std::string s;
    for (const auto& g : segs) s += g.raw;
    return s;
}

static void test_split()
{
    const std::string item = EncodeItem(46774);
    const std::string line = "Check out " + item + " and " + EncodeId(LINK_MAP, 1072) + "!";
    const auto segs = Split(line);
    CHECK(segs.size() == 5);
    CHECK(!segs[0].isLink && segs[0].raw == "Check out ");
    CHECK(segs[1].isLink && segs[1].raw == item && segs[1].type == LINK_ITEM && segs[1].id == 46774u);
    CHECK(segs[3].isLink && segs[3].type == LINK_MAP);
    CHECK(Join(segs) == line);

    CHECK(Split("").empty());
    const auto plain = Split("just plain text");
    CHECK(plain.size() == 1 && !plain[0].isLink && plain[0].raw == "just plain text");

    // Adjacent links, a link at the very start / end.
    const std::string two = item + item;
    const auto t = Split(two);
    CHECK(t.size() == 2 && t[0].isLink && t[1].isLink);
}

static void test_split_rejects_non_links()
{
    const char* const samples[] = {
        "[& not a link]", "[&]", "[&AgH1WgAA", "[&Ag#1WgAA]", "[[&", "a [&==] b",
        "Umlaute \xC3\xA4\xC3\xB6\xC3\xBC [&AgH1WgAA] \xC3\x9F",
    };
    for (const char* s : samples) {
        const auto segs = Split(s);
        CHECK(Join(segs) == s);
        for (const auto& g : segs)
            if (g.isLink) CHECK(g.raw == "[&AgH1WgAA]");
    }
    CHECK(TypeOf("not a link") == LINK_NONE);
    CHECK(TypeOf("") == LINK_NONE);
    CHECK(TypeOf("x[&AgH1WgAA]") == LINK_NONE);   // must be exactly one link
}

int main()
{
    test_base64_roundtrip();
    test_real_item_link();
    test_item_with_skin_and_upgrades();
    test_item_count_clamped();
    test_item_truncated_is_rejected();
    test_id_links();
    test_build_roundtrip();
    test_labels();
    test_split();
    test_split_rejects_non_links();

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
