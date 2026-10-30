// I18n.cpp — see I18n.h. Texts of the settings page (src/dllmain.cpp).
#include "core/I18n.h"

#include <cstring>

namespace Trailpack { namespace Core { namespace I18n {

const std::vector<std::pair<const char*, const char*>>& GermanTable()
{
    static const std::vector<std::pair<const char*, const char*>> kTable = {
        { "General",                                 "Allgemein" },
        { "Online",                                  "Online" },
        { "Offline only",                            "Nur offline" },
        { "Last error: %s",                          "Letzter Fehler: %s" },
        { "Game build: %u",                          "Spiel-Build: %u" },
        { "Game build: not known yet",               "Spiel-Build: noch unbekannt" },
        { "Language: %s",                            "Sprache: %s" },
        { "Follows the Nexus language; other languages use English.",
          "Folgt der Nexus-Sprache; andere Sprachen nutzen Englisch." },
        { "Requests in the last minute: %d",         "Anfragen in der letzten Minute: %d" },
        { "Waiting for data: %d",                    "Wartet auf Daten: %d" },
        { "No network requests: only cached and built-in data. Waypoints, builds and events always work.",
          "Keine Netzwerk-Anfragen: nur gespeicherte und eingebaute Daten. Wegmarken, Builds und Events "
          "funktionieren immer." },
        { "Cache",                                   "Cache" },
        { "Cached entries: %d English, %d German",   "Gespeicherte Einträge: %d Englisch, %d Deutsch" },
        { "Clear cache",                             "Cache leeren" },
        { "Clear the cache?",                        "Cache wirklich leeren?" },
        { "Yes",                                     "Ja" },
        { "No",                                      "Nein" },
        { "Names, icons and tooltips from the GW2 API are kept here so they show instantly next time. "
          "After clearing, Core fetches them again when needed.",
          "Namen, Icons und Tooltips aus der GW2-API werden hier gespeichert, damit sie beim nächsten Mal "
          "sofort da sind. Nach dem Leeren holt Core sie bei Bedarf neu." },
        { "About",                                   "Info" },
        { "Trailpack Core provides game data for all Trailpack addons. It only talks to the official GW2 API "
          "and the GW2 wiki, never touches the game and needs no API key.",
          "Trailpack Core stellt allen Trailpack-Addons Spieldaten bereit. Es spricht nur mit der offiziellen "
          "GW2-API und dem GW2-Wiki, greift nie ins Spiel ein und braucht keinen API-Key." },
        { "Built-in data: %d events, %d waypoints and points of interest.",
          "Eingebaute Daten: %d Events, %d Wegmarken und Sehenswürdigkeiten." },
    };
    return kTable;
}

const char* Tr(const char* en, Lang lang)
{
    if (lang != Lang::De || en == nullptr) return en;
    for (const auto& p : GermanTable())
        if (std::strcmp(p.first, en) == 0) return p.second;
    return en;
}

}}} // namespace Trailpack::Core::I18n
