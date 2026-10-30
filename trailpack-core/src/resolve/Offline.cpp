// Offline.cpp — see Offline.h.
#include "resolve/Offline.h"

#include <algorithm>

#include "chatlinks/ChatLinks.h"
#include "generated/GameData.h"

namespace Trailpack { namespace Core { namespace Offline {

using namespace GameData;
namespace CL = ChatLinks;

namespace {

const char* Pick(const char* en, const char* de, Lang lang)
{
    return lang == Lang::De && de != nullptr ? de : (en != nullptr ? en : "");
}

const SpecDef* FindSpec(uint16_t id)
{
    const SpecDef* end = kSpecs + kSpecCount;
    const SpecDef* it  = std::lower_bound(kSpecs, end, id, [](const SpecDef& s, uint16_t v) { return s.id < v; });
    return it != end && it->id == id ? it : nullptr;
}

} // namespace

bool Serves(uint8_t type)
{
    return type == CL::LINK_MAP || type == CL::LINK_BUILD;
}

bool Poi(uint32_t id, Lang lang, Record& out)
{
    const PoiDef* end = kPois + kPoiCount;
    const PoiDef* it  = std::lower_bound(kPois, end, id, [](const PoiDef& p, uint32_t v) { return p.id < v; });
    if (it == end || it->id != id) return false;
    out = Record{};
    out.type    = CL::LINK_MAP;
    out.id      = id;
    out.lang    = lang;
    out.name    = Pick(it->nameEn, it->nameDe, lang);
    out.poiKind = it->kind;
    if (it->map < kMapCount) out.mapName = Pick(kMaps[it->map].nameEn, kMaps[it->map].nameDe, lang);
    return true;
}

const char* SpecName(uint16_t specId, Lang lang)
{
    const SpecDef* s = FindSpec(specId);
    return s != nullptr ? Pick(s->nameEn, s->nameDe, lang) : nullptr;
}

bool SpecIsElite(uint16_t specId)
{
    const SpecDef* s = FindSpec(specId);
    return s != nullptr && s->elite != 0;
}

const char* ProfessionName(uint8_t code, Lang lang)
{
    if (code < 1 || code > kProfessionCount) return "";
    return Pick(kProfessions[code - 1].nameEn, kProfessions[code - 1].nameDe, lang);
}

bool Build(const std::string& chatCode, Lang lang, Record& out)
{
    CL::BuildLink b;
    if (!CL::DecodeBuild(chatCode, b)) return false;
    // The elite spec names the build; it sits in the third slot, but any slot counts.
    const char* label = nullptr;
    for (int i = 2; i >= 0 && label == nullptr; --i)
        if (SpecIsElite(b.specs[i].id)) label = SpecName(b.specs[i].id, lang);
    if (label == nullptr) label = ProfessionName(b.profession, lang);

    out = Record{};
    out.type = CL::LINK_BUILD;
    out.lang = lang;
    std::string l = label != nullptr ? label : "";
    out.name = l.empty() ? (lang == Lang::De ? "Build" : "Build")
                         : (lang == Lang::De ? l + "-Build" : l + " Build");
    // The specialisation lines, as facts ("Dueling", "Chaos", "Mirage").
    for (const auto& s : b.specs)
        if (const char* n = SpecName(s.id, lang)) out.facts.push_back({"", n});
    return true;
}

}}} // namespace Trailpack::Core::Offline
