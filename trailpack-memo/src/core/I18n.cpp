// I18n.cpp — English -> German UI string table.
#include "core/I18n.h"

#include <string_view>
#include <unordered_map>

namespace Trailpack { namespace I18n {

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
    { "No notes yet.",                     "Noch keine Notizen." },
    { "Click 'Create New Note' to start.", "Klicke auf 'Neue Notiz', um zu beginnen." },
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

    // ── Groups ────────────────────────────────────────────────────────────
    { "Menu",                              "Menü" },
    { "New group...",                      "Neue Gruppe..." },
    { "New group",                         "Neue Gruppe" },
    { "Remove from group",                 "Aus Gruppe entfernen" },
    { "Move to group",                     "In Gruppe verschieben" },
    { "New note in group",                 "Neue Notiz in Gruppe" },
    { "Rename group...",                   "Gruppe umbenennen..." },
    { "Export group...",                   "Gruppe exportieren..." },
    { "Delete group...",                   "Gruppe löschen..." },
    { "Group",                             "Gruppe" },
    { "Group name:",                       "Gruppenname:" },
    { "Delete Group",                      "Gruppe löschen" },
    { "Delete the group \"%s\"?",          "Die Gruppe \"%s\" löschen?" },
    { "Its notes are kept and moved out of the group.",
      "Ihre Notizen bleiben erhalten und werden aus der Gruppe genommen." },

    { "Group colour...",                   "Gruppenfarbe..." },
    { "Group Colour",                      "Gruppenfarbe" },
    { "Use default",                       "Standard verwenden" },
    { "Use the group colour from the addon options.",
      "Die Gruppenfarbe aus den Addon-Einstellungen verwenden." },
    { "Type",                              "Typ" },
    { "Note",                              "Notiz" },
    { "Daily",                             "Täglich" },
    { "Weekly",                            "Wöchentlich" },
    { "D",                                 "T" },
    { "W",                                 "W" },
    { "Daily reset in %s",                 "Täglicher Reset in %s" },
    { "Weekly reset in %s",                "Wöchentlicher Reset in %s" },
    { "Daily / Weekly: checkboxes are unchecked after the reset.",
      "Täglich / Wöchentlich: Haken werden nach dem Reset entfernt." },
    { "Delete note...",                    "Notiz löschen..." },
    { "Settings...",                       "Einstellungen..." },
    { "Delete notes...",                   "Notizen löschen..." },
    { "Delete Notes",                      "Notizen löschen" },
    { "Select the notes to delete:",       "Notizen zum Löschen auswählen:" },
    { "Select all",                        "Alle auswählen" },
    { "Delete 1 note",                     "1 Notiz löschen" },
    { "Delete %d notes",                   "%d Notizen löschen" },
    { "Really delete 1 note? This cannot be undone.",
      "Wirklich 1 Notiz löschen? Das kann nicht rückgängig gemacht werden." },
    { "Really delete %d notes? This cannot be undone.",
      "Wirklich %d Notizen löschen? Das kann nicht rückgängig gemacht werden." },
    { "Back",                              "Zurück" },

    // ── Loading ───────────────────────────────────────────────────────────
    { "Notice",                            "Hinweis" },
    { "notes.json could not be read and was kept as %s.\n"
      "Your notes were restored from the last backup.",
      "notes.json konnte nicht gelesen werden und wurde als %s aufbewahrt.\n"
      "Deine Notizen wurden aus der letzten Sicherung wiederhergestellt." },
    { "notes.json could not be read and was kept as %s.\n"
      "No backup could be loaded, so the list starts empty.",
      "notes.json konnte nicht gelesen werden und wurde als %s aufbewahrt.\n"
      "Es konnte keine Sicherung geladen werden, die Liste startet leer." },

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

    // ── Guide (HelpWindow) ────────────────────────────────────────────────
    { "Guide",                             "Anleitung" },
    { "Getting started",                   "Erste Schritte" },
    { "Trailpack Memo keeps notes and checklists next to the game.\n"
      "- Open / close the window with CTRL+SHIFT+T or the quick-access icon.\n"
      "- Click 'Create New Note', type a title and text, then 'Save'.\n"
      "- Click a note in the list to show it, 'Edit' (or right-click > 'Edit') to change it.",
      "Trailpack Memo hält Notizen und Checklisten direkt im Spiel bereit.\n"
      "- Fenster öffnen / schließen mit STRG+UMSCHALT+T oder dem Schnellzugriff-Icon.\n"
      "- Auf 'Neue Notiz' klicken, Titel und Text eingeben, dann 'Speichern'.\n"
      "- Eine Notiz in der Liste anklicken zeigt sie an, 'Bearbeiten' (oder Rechtsklick > "
      "'Bearbeiten') ändert sie." },
    { "Writing notes",                     "Notizen schreiben" },
    { "Notes use simple markdown; the edit view shows the raw text.\n"
      "- **bold**, *italic*, # Heading, - bullet list\n"
      "- Toolbar buttons insert or remove these for the selection; CTRL+B / CTRL+I too.\n"
      "- Enter in a list continues it; Enter on an empty item ends it.\n"
      "- 'Preview' shows the rendered note beside the editor.",
      "Notizen nutzen einfaches Markdown; beim Bearbeiten siehst du den Rohtext.\n"
      "- **fett**, *kursiv*, # Überschrift, - Aufzählung\n"
      "- Die Toolbar-Buttons fügen das für die Auswahl ein oder entfernen es; auch STRG+B / STRG+I.\n"
      "- Enter in einer Liste setzt sie fort; Enter auf einem leeren Punkt beendet sie.\n"
      "- 'Vorschau' zeigt die fertige Notiz neben dem Editor." },
    { "Checklists",                        "Checklisten" },
    { "- A line '- [ ] task' becomes a checkbox; '- [x] task' is ticked.\n"
      "- Click a box in the note view to tick it - saved immediately.\n"
      "- The list shows progress (3/7, green when complete).\n"
      "- Right-click the note > 'Uncheck all tasks' to start over.",
      "- Eine Zeile '- [ ] Aufgabe' wird zur Checkbox; '- [x] Aufgabe' ist abgehakt.\n"
      "- In der Ansicht ein Kästchen anklicken hakt es ab - sofort gespeichert.\n"
      "- Die Liste zeigt den Fortschritt (3/7, grün wenn fertig).\n"
      "- Rechtsklick auf die Notiz > 'Alle Haken entfernen' fängt von vorne an." },
    { "Daily and weekly lists",            "Tägliche und wöchentliche Listen" },
    { "Give a note a type: right-click > 'Type', or the selector next to the title while editing.\n"
      "- Daily: all boxes are unticked after the daily reset (00:00 UTC).\n"
      "- Weekly: after the weekly reset (Monday 07:30 UTC).\n"
      "- A coloured D / W badge marks them; hover it for the time to the next reset.\n"
      "- Missed resets (game closed) are caught up when the window is opened.",
      "Gib einer Notiz einen Typ: Rechtsklick > 'Typ' oder die Auswahl neben dem Titel beim Bearbeiten.\n"
      "- Täglich: alle Haken werden nach dem täglichen Reset entfernt (00:00 UTC).\n"
      "- Wöchentlich: nach dem wöchentlichen Reset (Montag 07:30 UTC).\n"
      "- Ein farbiges T / W markiert sie; darüberfahren zeigt die Zeit bis zum nächsten Reset.\n"
      "- Verpasste Resets (Spiel war zu) werden beim Öffnen des Fensters nachgeholt." },
    { "Groups",                            "Gruppen" },
    { "- Menu button (next to 'Create New Note') > 'New group...', or right-click an empty spot "
      "in the list (also: new note, delete notes).\n"
      "- Click a group to fold / unfold it. Drag notes onto it, or right-click a note > "
      "'Move to group'. To take a note out of its group, drag it to an empty spot in the "
      "list or use 'Move to group' > 'Remove from group'.\n"
      "- Order: drag a note onto another note (it goes above or below it, into that note's "
      "group), or a group onto another group.\n"
      "- Right-click a group: new note in it, rename, colour, export, delete (its notes are kept).",
      "- Menü-Button (neben 'Neue Notiz') > 'Neue Gruppe...' oder Rechtsklick auf eine leere "
      "Stelle in der Liste (dort auch: neue Notiz, Notizen löschen).\n"
      "- Eine Gruppe anklicken klappt sie auf / zu. Notizen darauf ziehen oder Rechtsklick auf "
      "eine Notiz > 'In Gruppe verschieben'. Aus der Gruppe nehmen: auf eine leere Stelle in "
      "der Liste ziehen oder 'In Gruppe verschieben' > 'Aus Gruppe entfernen'.\n"
      "- Reihenfolge: eine Notiz auf eine andere ziehen (sie landet darüber oder darunter, in "
      "deren Gruppe) oder eine Gruppe auf eine andere.\n"
      "- Rechtsklick auf eine Gruppe: neue Notiz darin, umbenennen, Farbe, exportieren, löschen "
      "(ihre Notizen bleiben erhalten)." },
    { "Chat links",                        "Chat-Links" },
    { "Paste GW2 chat links ([&...]) into a note - they become chips.\n"
      "- With the Decoder Ring addon: icon, name and a detailed tooltip.\n"
      "- Right-click a chip: copy the chat code or open the wiki.\n"
      "- With Pie UI: left-click a waypoint chip to open the map there.",
      "Füge GW2-Chat-Links ([&...]) in eine Notiz ein - sie werden zu Chips.\n"
      "- Mit dem Addon Decoder Ring: Icon, Name und ausführlicher Tooltip.\n"
      "- Rechtsklick auf einen Chip: Chatcode kopieren oder Wiki öffnen.\n"
      "- Mit Pie UI: Linksklick auf einen Wegpunkt-Chip öffnet dort die Karte." },
    { "Event timers",                      "Event-Timer" },
    { "While editing, 'Event' in the toolbar picks an event: search by name or map, or browse "
      "the categories. It becomes an event chip; {{event:<id>}} typed by hand works too.\n"
      "- Or type /e and a name (e.g. /e teq): pick with the arrow keys and Enter, or click; "
      "Escape closes the list.\n"
      "- It shows the time until the next start, or since when the event is running.\n"
      "- Hover it for the next starts in your local time, the duration, map and waypoint.\n"
      "- Left-click copies the waypoint; right-click: copy waypoint or open the wiki.\n"
      "- While editing, the chip shows the event name only.",
      "Beim Bearbeiten wählt 'Event' in der Werkzeugleiste ein Event: Suche nach Name oder Karte, "
      "oder in den Kategorien stöbern. Es wird zum Event-Chip; von Hand geschriebenes "
      "{{event:<id>}} geht auch.\n"
      "- Oder /e und einen Namen tippen (z. B. /e teq): mit den Pfeiltasten und Enter wählen "
      "oder klicken; Escape schließt die Liste.\n"
      "- Er zeigt die Zeit bis zum nächsten Start oder seit wann das Event läuft.\n"
      "- Beim Darüberfahren: nächste Starts in deiner Ortszeit, Dauer, Karte und Wegpunkt.\n"
      "- Linksklick kopiert den Wegpunkt; Rechtsklick: Wegpunkt kopieren oder Wiki öffnen.\n"
      "- Beim Bearbeiten zeigt der Chip nur den Namen des Events." },
    { "Import, export and deleting",       "Import, Export und Löschen" },
    { "All in the menu button and the right-click menus.\n"
      "- 'Export all...' saves every note and group to one file; right-click a note or group "
      "to export just that.\n"
      "- 'Import...' lists the notes in a file to pick from. Notes you already have are skipped; "
      "a changed version can be applied with 'Update'.\n"
      "- 'Delete notes...' deletes several notes at once; right-click > 'Delete note...' "
      "deletes one.",
      "Alles im Menü-Button und in den Rechtsklick-Menüs.\n"
      "- 'Alle exportieren...' speichert alle Notizen und Gruppen in eine Datei; Rechtsklick auf "
      "eine Notiz oder Gruppe exportiert nur diese.\n"
      "- 'Importieren...' listet die Notizen einer Datei zur Auswahl. Vorhandene Notizen werden "
      "übersprungen; eine geänderte Version lässt sich mit 'Aktualisieren' übernehmen.\n"
      "- 'Notizen löschen...' löscht mehrere Notizen auf einmal; Rechtsklick > 'Notiz löschen...' "
      "löscht eine." },
    { "Settings",                          "Einstellungen" },
    { "Nexus addon options > Trailpack Memo, or menu button > 'Settings...':\n"
      "- 'General': quick access icon, language (Auto follows Nexus), Decoder Ring status.\n"
      "- 'Font options': UI and note text size.\n"
      "- 'Layout options': corner rounding of the addon's windows, buttons, fields, popups and list rows (0 = square); 'Groups above single notes' puts the groups at the top of the list (default: below the single notes); 'List row spacing' sets the space between the rows of the note list.\n"
      "- 'Colour options': window (border on/off and colour, background, text, menu button and icon), note list (border on/off and colour, background, text, group names), note (border on/off and colour, background, text), note editor (border on/off and colour), popups (background, text) and the daily / weekly badges.\n"
      "- 'Colour options' > Theme: Default, Light, Dark or one of your own; 'Save as theme' "
      "stores the current colours under a name. Borders are off by default.\n"
      "- 'Events': event chips in checked tasks keep their time / tooltip and menu (both off by default).\n"
      "- 'Data: backup and transfer' (at the bottom): 'Create backup...' saves all notes, groups and "
      "settings in one file, 'Restore backup...' replaces them with a backup's; 'Export settings...' "
      "saves the settings (incl. themes) without notes, 'Import settings...' replaces the settings "
      "and keeps the notes. Both ask before replacing anything.\n"
      "- Drag the divider between list and note to change the list width.",
      "Nexus-Addon-Optionen > Trailpack Memo, oder Menü-Knopf > 'Einstellungen...':\n"
      "- 'Allgemein': Schnellzugriff-Icon, Sprache (Automatisch folgt Nexus), Decoder-Ring-Status.\n"
      "- 'Schriftoptionen': UI- und Notiz-Textgröße.\n"
      "- 'Layout-Optionen': Eckenrundung der Addon-Fenster, Buttons, Felder, Popups und Listenzeilen (0 = eckig); 'Gruppen über einzelnen Notizen' stellt die Gruppen an den Anfang der Liste (Standard: unter den einzelnen Notizen); 'Zeilenabstand der Liste' legt den Abstand zwischen den Zeilen der Notizliste fest.\n"
      "- 'Farboptionen': Fenster (Rahmen an/aus und Farbe, Hintergrund, Text, Menü-Knopf und -Symbol), Notizliste (Rahmen an/aus und Farbe, Hintergrund, Text, Gruppennamen), Notiz (Rahmen an/aus und Farbe, Hintergrund, Text), Notiz-Editor (Rahmen an/aus und Farbe), Popups (Hintergrund, Text) und die Täglich-/Wöchentlich-Badges.\n"
      "- 'Farboptionen' > Theme: Standard, Hell, Dunkel oder ein eigenes; 'Als Theme speichern' "
      "sichert die aktuellen Farben unter einem Namen. Rahmen sind standardmäßig aus.\n"
      "- 'Events': Event-Chips in erledigten Aufgaben behalten Zeit / Tooltip und Menü (beide standardmäßig aus).\n"
      "- 'Daten: Backup und Übertragung' (ganz unten): 'Backup erstellen...' speichert alle Notizen, "
      "Gruppen und Einstellungen in einer Datei, 'Backup wiederherstellen...' ersetzt sie durch die "
      "des Backups; 'Einstellungen exportieren...' speichert die Einstellungen (inklusive Themes) "
      "ohne Notizen, 'Einstellungen importieren...' ersetzt die Einstellungen und behält die "
      "Notizen. Beides fragt vor dem Ersetzen nach.\n"
      "- Den Trenner zwischen Liste und Notiz ziehen ändert die Breite der Liste." },
    { "Your data",                         "Deine Daten" },
    { "Notes are saved in addons/TrailpackMemo/notes.json; the previous version is kept as "
      "notes.json.bak. A damaged file is never overwritten - it is kept aside and the backup "
      "is loaded instead.",
      "Notizen werden in addons/TrailpackMemo/notes.json gespeichert; die vorherige Version "
      "bleibt als notes.json.bak erhalten. Eine beschädigte Datei wird nie überschrieben - sie "
      "wird aufbewahrt und stattdessen die Sicherung geladen." },

    // ── Options (dllmain) ─────────────────────────────────────────────────
    { "Toggle Trailpack Memo",             "Trailpack Memo ein-/ausblenden" },
    { "Decoder Ring: connected",           "Decoder Ring: verbunden" },
    { "Chat-link chips: basic labels (optional: Decoder Ring addon)",
      "Chatlink-Chips: einfache Bezeichnungen (optional: Addon Decoder Ring)" },
    { "Decoder Ring is an optional addon. With it, chat-link chips "
      "([&...]) show the real name, icon and a detailed tooltip; "
      "without it they show basic labels like [Item].\n"
      "Everything else works either way: copy code, open in wiki "
      "and event chips.",
      "Decoder Ring ist ein optionales Addon. Damit zeigen Chatlink-Chips ([&...]) den echten "
      "Namen, ein Icon und einen ausführlichen Tooltip; ohne zeigen sie einfache Bezeichnungen "
      "wie [Item].\nAlles andere funktioniert so oder so: Chatcode kopieren, im Wiki öffnen "
      "und Event-Chips." },
    { "Language",                          "Sprache" },
    { "Auto (Nexus language)",             "Automatisch (Nexus-Sprache)" },
    { "Show quick access icon",            "Schnellzugriff-Icon anzeigen" },
    { "UI text size",                      "UI-Textgröße" },
    { "Note text size",                    "Notiz-Textgröße" },
    { "Reset",                             "Zurücksetzen" },
    { "Font options",                      "Schriftoptionen" },
    { "Layout options",                    "Layout-Optionen" },
    { "Show the time in checked tasks",    "Zeit in erledigten Aufgaben anzeigen" },
    { "Event chips in checked tasks keep their countdown, in a much lighter colour.",
      "Event-Chips in erledigten Aufgaben behalten ihren Countdown, in deutlich hellerer Farbe." },
    { "Show tooltip and menu in checked tasks", "Tooltip und Menü in erledigten Aufgaben anzeigen" },
    { "Event chips in checked tasks keep their tooltip and right-click menu (wiki, copy waypoint). Left-click does nothing.",
      "Event-Chips in erledigten Aufgaben behalten Tooltip und Rechtsklick-Menü (Wiki, Wegpunkt kopieren). Linksklick tut nichts." },
    { "Corner rounding",                   "Eckenrundung" },
    { "List row spacing",                  "Zeilenabstand der Liste" },
    { "Colour options",                    "Farboptionen" },
    { "Border",                            "Rahmen" },
    { "Background",                        "Hintergrund" },
    { "Text",                              "Text" },
    { "Group names",                       "Gruppennamen" },
    { "Group names follow the border until set; right-click a group for its own colour.",
      "Gruppennamen folgen dem Rahmen, bis gesetzt; Rechtsklick auf eine Gruppe für eine eigene Farbe." },
    { "Window",                            "Fenster" },
    { "Show border",                       "Rahmen anzeigen" },
    { "Note list",                         "Notizliste" },
    { "Note editor",                       "Notiz-Editor" },
    { "Groups above single notes",         "Gruppen über einzelnen Notizen" },
    { "Popups",                            "Popups" },
    { "Daily / weekly badges",             "Täglich-/Wöchentlich-Badges" },
    { "Reset all colours",                 "Alle Farben zurücksetzen" },
    { "Menu button",                       "Menü-Knopf" },
    { "Menu icon",                         "Menü-Symbol" },
    { "Theme",                             "Theme" },

    // ── Events (chip times) ───────────────────────────────────────────────
    { "in %d s",                           "in %d s" },
    { "in %d min",                         "in %d Min" },
    { "in %d h",                           "in %d Std" },
    { "in %d h %d min",                    "in %d Std %d Min" },
    { "for %d s",                          "seit %d s" },
    { "for %d min",                        "seit %d Min" },
    { "for %d h",                          "seit %d Std" },
    { "for %d h %d min",                   "seit %d Std %d Min" },
    { "%d min",                            "%d Min" },
    { "%d h",                              "%d Std" },
    { "%d h %d min",                       "%d Std %d Min" },
    { "Unknown event (%s)",                "Unbekanntes Event (%s)" },
    { "Running since %s, ends at %s",      "Läuft seit %s, endet um %s" },
    { "Next starts: %s",                   "Nächste Starts: %s" },
    { "Duration: %s",                      "Dauer: %s" },
    { "Map: %s",                           "Karte: %s" },
    { "Waypoint: %s",                      "Wegpunkt: %s" },
    { "Left-click: copy waypoint. Right-click: more.",
      "Linksklick: Wegpunkt kopieren. Rechtsklick: mehr." },
    { "No waypoint known for this event.", "Für dieses Event ist kein Wegpunkt bekannt." },
    { "Waypoint copied",                   "Wegpunkt kopiert" },
    { "Copy waypoint",                     "Wegpunkt kopieren" },
    { "Event",                             "Event" },
    { "Insert an event timer",             "Event-Timer einfügen" },
    { "Search events...",                  "Events suchen..." },
    { "No events found.",                  "Keine Events gefunden." },
    { "Save as theme",                     "Als Theme speichern" },
    { "Default (Nexus)",                   "Standard (Nexus)" },
    { "Light",                             "Hell" },
    { "Dark",                              "Dunkel" },
    { "Custom",                            "Eigene" },
    { "Delete theme",                      "Theme löschen" },
    { "Name",                              "Name" },
    { "Overwrite",                         "Überschreiben" },
    { "Backup",                            "Backup" },
    { "A backup saves all notes, groups and settings (including themes) in one file.",
      "Ein Backup speichert alle Notizen, Gruppen und Einstellungen (inklusive Themes) in einer Datei." },
    { "Create backup...",                  "Backup erstellen..." },
    { "Restore backup...",                 "Backup wiederherstellen..." },
    { "Create backup",                     "Backup erstellen" },
    { "Restore backup",                    "Backup wiederherstellen" },
    { "Trailpack Memo backup (*.json)",    "Trailpack-Memo-Backup (*.json)" },
    { "Backup saved as %s.",               "Backup gespeichert als %s." },
    { "%s is not a Trailpack Memo backup. Nothing was changed.",
      "%s ist kein Trailpack-Memo-Backup. Es wurde nichts geändert." },
    { "The backup could not be restored. Nothing was changed.",
      "Das Backup konnte nicht wiederhergestellt werden. Es wurde nichts geändert." },
    { "Backup restored: %d note.",         "Backup wiederhergestellt: %d Notiz." },
    { "Backup restored: %d notes.",        "Backup wiederhergestellt: %d Notizen." },
    { "Restore %s (%d note)? This replaces all notes and settings.",
      "%s (%d Notiz) wiederherstellen? Das ersetzt alle Notizen und Einstellungen." },
    { "Restore %s (%d notes)? This replaces all notes and settings.",
      "%s (%d Notizen) wiederherstellen? Das ersetzt alle Notizen und Einstellungen." },
    { "Restore",                           "Wiederherstellen" },
    { "General",                           "Allgemein" },
    { "Data: backup and transfer",         "Daten: Backup und Übertragung" },
    { "Saves all settings (including themes) without notes. "
      "Importing replaces all settings; the notes stay as they are. "
      "A backup file can be imported too (only its settings).",
      "Speichert alle Einstellungen (inklusive Themes) ohne Notizen. "
      "Importieren ersetzt alle Einstellungen; die Notizen bleiben, wie sie sind. "
      "Auch eine Backup-Datei lässt sich importieren (nur ihre Einstellungen)." },
    { "Export settings...",                "Einstellungen exportieren..." },
    { "Import settings...",                "Einstellungen importieren..." },
    { "Export settings",                   "Einstellungen exportieren" },
    { "Import settings",                   "Einstellungen importieren" },
    { "Trailpack Memo settings (*.json)",  "Trailpack-Memo-Einstellungen (*.json)" },
    { "Settings saved as %s.",             "Einstellungen gespeichert als %s." },
    { "%s contains no Trailpack Memo settings. Nothing was changed.",
      "%s enthält keine Trailpack-Memo-Einstellungen. Es wurde nichts geändert." },
    { "Import settings from %s? This replaces all settings.",
      "Einstellungen aus %s importieren? Das ersetzt alle Einstellungen." },
    { "Settings imported from %s.",        "Einstellungen aus %s importiert." },
    { "Import",                            "Importieren" },
    { "Reset uses the Nexus default (gold for the border).",
      "Zurücksetzen nutzt den Nexus-Standard (Gold für den Rahmen)." },
};

// Keys view the static English literals in kGerman: lookups hash the text but never
// allocate (Tr runs many times per frame).
static const std::unordered_map<std::string_view, const char*>& GermanMap()
{
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> m;
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
    const auto it = map.find(std::string_view(en));
    return it != map.end() ? it->second : en;
}

const char* TrN(long long n, const char* one, const char* many)
{
    return Tr(n == 1 ? one : many);
}

const std::vector<std::pair<const char*, const char*>>& GermanTable() { return kGerman; }

}} // namespace Trailpack::I18n
