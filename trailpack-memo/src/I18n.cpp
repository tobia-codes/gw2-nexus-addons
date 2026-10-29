// I18n.cpp — English -> German UI string table.
#include "I18n.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace Magpie { namespace I18n {

static Lang s_Lang = Lang::En;

// English key -> German text. Keep entries grouped by the file that uses them.
static const std::vector<std::pair<const char*, const char*>> kGerman = {
    // ── NotesWindow ───────────────────────────────────────────────────────
    { "Untitled note",                     "Unbenannte Notiz" },
    { "Unsaved Changes",                   "Ungespeicherte Änderungen" },
    { "This note has unsaved changes.",    "Diese Notiz hat ungespeicherte Änderungen." },
    { "Save",                              "Speichern" },
    { "Discard",                           "Verwerfen" },
    { "Cancel",                            "Abbrechen" },
    { "Delete",                            "Löschen" },
    { "Edit",                              "Bearbeiten" },
    { "OK",                                "OK" },
    { "Create New Note",                   "Neue Notiz" },
    { "Import...",                         "Importieren..." },
    { "Export all...",                     "Alle exportieren..." },
    { "Export note...",                    "Notiz exportieren..." },
    { "Add notes from an exported file (or a notes.json).\n"
      "Existing notes are only updated if you choose to.",
      "Notizen aus einer exportierten Datei (oder einer notes.json) hinzufügen.\n"
      "Vorhandene Notizen werden nur aktualisiert, wenn du es auswählst." },
    { "Save all notes to one file.\n"
      "Right-click a note in the list to export just that one.",
      "Alle Notizen in einer Datei speichern.\n"
      "Rechtsklick auf eine Notiz in der Liste exportiert nur diese." },
    { "Your nest is empty.",               "Dein Nest ist leer." },
    { "Click 'Create New Note' to start collecting.",
      "Klicke auf 'Neue Notiz', um mit dem Sammeln zu beginnen." },
    { "Bold (Ctrl+B)",                     "Fett (Strg+B)" },
    { "Italic (Ctrl+I)",                   "Kursiv (Strg+I)" },
    { "Heading 1",                         "Überschrift 1" },
    { "Heading 2",                         "Überschrift 2" },
    { "- List",                            "- Liste" },
    { "Bullet list",                       "Aufzählung" },
    { "[ ] Task",                          "[ ] Aufgabe" },
    { "Checkbox (task list)",              "Checkbox (Aufgabenliste)" },
    { "Preview",                           "Vorschau" },
    { "Show a live preview beside the editor", "Live-Vorschau neben dem Editor anzeigen" },
    { "Uncheck all tasks",                 "Alle Haken entfernen" },
    { "Confirm Delete",                    "Löschen bestätigen" },
    { "Delete this note? This cannot be undone.",
      "Diese Notiz löschen? Das kann nicht rückgängig gemacht werden." },

    // ── Export / import ───────────────────────────────────────────────────
    { "Export notes",                      "Notizen exportieren" },
    { "Import notes",                      "Notizen importieren" },
    { "Trailpack Memo notes (*.json)",     "Trailpack-Memo-Notizen (*.json)" },
    { "All files (*.*)",                   "Alle Dateien (*.*)" },
    { "Exported 1 note to %s.",            "1 Notiz nach %s exportiert." },
    { "Exported %d notes to %s.",          "%d Notizen nach %s exportiert." },
    { "Could not write %s.",               "%s konnte nicht geschrieben werden." },
    { "%s is not a Trailpack Memo export or notes file.\nNothing was imported.",
      "%s ist keine Trailpack-Memo-Export- oder Notizdatei.\nEs wurde nichts importiert." },
    { "%s contains no notes.",             "%s enthält keine Notizen." },
    { "All notes in %s already exist and are up to date.\nNothing was imported.",
      "Alle Notizen in %s sind bereits vorhanden und aktuell.\nEs wurde nichts importiert." },
    { "Import / Export",                   "Import / Export" },
    { "Import Notes",                      "Notizen importieren" },
    { "Notes in %s:",                      "Notizen in %s:" },
    { "(untitled)",                        "(ohne Titel)" },
    { "Update: %s",                        "Aktualisieren: %s" },
    { "You already have this note, but the file's version differs.\n"
      "Tick to replace your title and text with the file's version.",
      "Diese Notiz hast du schon, aber die Version in der Datei ist anders.\n"
      "Anhaken, um Titel und Text durch die Version aus der Datei zu ersetzen." },
    { "[-] %s  (open in the editor - save or cancel first)",
      "[-] %s  (im Editor geöffnet - erst speichern oder abbrechen)" },
    { "[-] %s  (already up to date)",      "[-] %s  (bereits aktuell)" },
    { "All",                               "Alle" },
    { "None",                              "Keine" },
    { "Import %d, update %d",              "%d importieren, %d aktualisieren" },
    { "Update 1 note",                     "1 Notiz aktualisieren" },
    { "Update %d notes",                   "%d Notizen aktualisieren" },
    { "Import 1 note",                     "1 Notiz importieren" },
    { "Import %d notes",                   "%d Notizen importieren" },

    // ── Chips (ChipRich) ──────────────────────────────────────────────────
    { "Open map in Pie UI",                "Karte in Pie UI öffnen" },
    { "Open wardrobe template in Pie UI",  "Garderobenvorlage in Pie UI öffnen" },
    { "Open build in Pie UI",              "Build in Pie UI öffnen" },
    { "Preview in Pie UI",                 "Vorschau in Pie UI" },
    { "Account Bound on Acquire",          "Accountgebunden beim Erwerb" },
    { "Soulbound on Acquire",              "Seelengebunden beim Erwerb" },
    { "Account Bound on Use",              "Accountgebunden bei Benutzung" },
    { "Soulbound on Use",                  "Seelengebunden bei Benutzung" },
    { "(unnamed)",                         "(ohne Namen)" },
    { "Vendor: ",                          "Händlerwert: " },
    { "Tradeable on the Trading Post",     "Am Handelsposten handelbar" },
    { "Not tradeable",                     "Nicht handelbar" },
    { "Cannot be sold to a vendor",        "Kann nicht an Händler verkauft werden" },
    { "Upgrades:",                         "Aufwertungen:" },
    { "Map: ",                             "Karte: " },
    { "Decoder Ring not loaded.",          "Decoder Ring nicht geladen." },
    { "Right-click to copy the code or open the wiki.",
      "Rechtsklick: Code kopieren oder Wiki öffnen." },
    { "Left-click: %s",                    "Linksklick: %s" },
    { "Copy chat code",                    "Chatcode kopieren" },
    { "Open in wiki",                      "Im Wiki öffnen" },

    // ── Options (dllmain) ─────────────────────────────────────────────────
    { "Toggle Trailpack Memo",             "Trailpack Memo ein-/ausblenden" },
    { "Decoder Ring: connected -- chips show full names and icons.",
      "Decoder Ring: verbunden -- Chips zeigen volle Namen und Icons." },
    { "Decoder Ring not loaded -- chips show basic labels.",
      "Decoder Ring nicht geladen -- Chips zeigen einfache Bezeichnungen." },
    { "Copy code and Open in wiki still work.",
      "Chatcode kopieren und Im Wiki öffnen funktionieren weiterhin." },
    { "Language",                          "Sprache" },
    { "Auto (Nexus language)",             "Automatisch (Nexus-Sprache)" },
    { "Show quick access icon",            "Schnellzugriff-Icon anzeigen" },
    { "UI text size",                      "UI-Textgröße" },
    { "Note text size",                    "Notiz-Textgröße" },
    { "Reset",                             "Zurücksetzen" },
    { "Window colours",                    "Fensterfarben" },
    { "Border",                            "Rahmen" },
    { "Background",                        "Hintergrund" },
    { "Text",                              "Text" },
    { "Reset uses the Nexus default (gold for the border).",
      "Zurücksetzen nutzt den Nexus-Standard (Gold für den Rahmen)." },
};

static const std::unordered_map<std::string, const char*>& GermanMap()
{
    static const std::unordered_map<std::string, const char*> map = [] {
        std::unordered_map<std::string, const char*> m;
        for (const auto& e : kGerman) m.emplace(e.first, e.second);
        return m;
    }();
    return map;
}

void SetLang(Lang lang) { s_Lang = lang; }
Lang Current()          { return s_Lang; }

const char* Tr(const char* en)
{
    if (en == nullptr || s_Lang == Lang::En) return en;
    const auto& map = GermanMap();
    const auto it = map.find(en);
    return it != map.end() ? it->second : en;
}

const char* TrN(long long n, const char* one, const char* many)
{
    return Tr(n == 1 ? one : many);
}

const std::vector<std::pair<const char*, const char*>>& GermanTable() { return kGerman; }

}} // namespace Magpie::I18n
