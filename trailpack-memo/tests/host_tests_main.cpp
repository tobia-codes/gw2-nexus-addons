#include "NotesStore.h"
#include "chat/ChatLinks.h"
#include "chat/SpecData.h"
#include "Markdown.h"
#include "MarkdownEdit.h"
#include "ChipResolver.h"
#include "ChipCodec.h"
#include "IconUrl.h"
#include "I18n.h"
#include "Reset.h"
#include <set>
#include <vector>
#include "ChipColor.h"
#include "DecoderRingApi.h"
#include <iostream>
#include <string>
#include <cstdint>

// ── Minimal test harness ──────────────────────────────────────────────────────

static int s_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n"; \
            ++s_failures; \
        } \
    } while (false)

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " \
                      << #a " == " #b " (got \"" << _a << "\" vs \"" << _b << "\")\n"; \
            ++s_failures; \
        } \
    } while (false)

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_create_appears_in_notes()
{
    Magpie::NotesStore store;
    auto& n = store.Create("My note");
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].title, std::string("My note"));
    CHECK(store.Notes()[0].body.empty());
    CHECK(!store.Notes()[0].id.empty());
    // Returned reference matches what's in the list
    CHECK_EQ(n.id, store.Notes()[0].id);
}

static void test_default_title()
{
    Magpie::NotesStore store;
    store.Create();
    CHECK_EQ(store.Notes()[0].title, std::string("Untitled note"));
}

static void test_two_creates_have_different_ids()
{
    Magpie::NotesStore store;
    std::string idA = store.Create("A").id;
    std::string idB = store.Create("B").id;
    CHECK(idA != idB);
    CHECK(store.Notes().size() == 2);
}

static void test_get_returns_note()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Find me");
    Magpie::Note* p = store.Get(n.id);
    CHECK(p != nullptr);
    CHECK_EQ(p->title, std::string("Find me"));
}

static void test_get_returns_nullptr_for_missing()
{
    Magpie::NotesStore store;
    store.Create("something");
    Magpie::Note* p = store.Get("nonexistent-id");
    CHECK(p == nullptr);
}

static void test_edit_via_get_persists()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Original");
    Magpie::Note* p = store.Get(n.id);
    p->body = "edited body";
    p->title = "Updated";
    CHECK_EQ(store.Notes()[0].body, std::string("edited body"));
    CHECK_EQ(store.Notes()[0].title, std::string("Updated"));
}

static void test_edit_via_create_ref_persists()
{
    Magpie::NotesStore store;
    auto& n = store.Create("Ref test");
    n.body = "body via ref";
    CHECK_EQ(store.Notes()[0].body, std::string("body via ref"));
}

static void test_delete_removes_note()
{
    Magpie::NotesStore store;
    std::string aid = store.Create("A").id;
    std::string bid = store.Create("B").id;
    store.Delete(aid);
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].id, bid);
    CHECK(store.Get(aid) == nullptr);
}

static void test_delete_noop_for_missing()
{
    Magpie::NotesStore store;
    store.Create("X");
    store.Delete("does-not-exist");
    CHECK(store.Notes().size() == 1);
}

static void test_json_roundtrip_ordered()
{
    Magpie::NotesStore src;
    std::string id1 = src.Create("First").id;
    src.Get(id1)->body = "body one";
    std::string id2 = src.Create("Second").id;
    src.Get(id2)->body = "body two";
    std::string id3 = src.Create("Third").id;
    src.Get(id3)->body = "body three";

    std::string json = src.ToJson();
    CHECK(!json.empty());

    Magpie::NotesStore dst;
    dst.Create("should be replaced");
    bool ok = dst.FromJson(json);
    CHECK(ok);
    CHECK(dst.Notes().size() == 3);
    // Order preserved
    CHECK_EQ(dst.Notes()[0].id, id1);
    CHECK_EQ(dst.Notes()[0].title, std::string("First"));
    CHECK_EQ(dst.Notes()[0].body, std::string("body one"));
    CHECK_EQ(dst.Notes()[1].id, id2);
    CHECK_EQ(dst.Notes()[1].title, std::string("Second"));
    CHECK_EQ(dst.Notes()[1].body, std::string("body two"));
    CHECK_EQ(dst.Notes()[2].id, id3);
    CHECK_EQ(dst.Notes()[2].title, std::string("Third"));
    CHECK_EQ(dst.Notes()[2].body, std::string("body three"));
}

static void test_fromjson_invalid_returns_false_and_leaves_state()
{
    Magpie::NotesStore store;
    store.Create("keep me");
    bool ok = store.FromJson("{ not valid json !!!");
    CHECK(!ok);
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].title, std::string("keep me"));
}

static void test_fromjson_wrong_shape_returns_false()
{
    // Valid JSON but not an array of note objects
    Magpie::NotesStore store;
    store.Create("keep me");
    bool ok = store.FromJson("{\"hello\":\"world\"}");
    CHECK(!ok);
    CHECK(store.Notes().size() == 1);
}

static void test_fromjson_empty_array()
{
    Magpie::NotesStore store;
    store.Create("will be cleared");
    bool ok = store.FromJson("[]");
    CHECK(ok);
    CHECK(store.Notes().empty());
}

static void test_create_after_fromjson_has_unique_id()
{
    // Regression: loading notes from disk must advance the id counter so a
    // subsequent Create() cannot mint an id that collides with a loaded one.
    Magpie::NotesStore store;
    bool ok = store.FromJson(
        "[{\"id\":\"00000000\",\"title\":\"A\",\"body\":\"\"},"
        " {\"id\":\"00000001\",\"title\":\"B\",\"body\":\"\"}]");
    CHECK(ok);
    std::string newId = store.Create("C").id;
    CHECK(newId != std::string("00000000"));
    CHECK(newId != std::string("00000001"));
    // And the new note is independently addressable.
    CHECK(store.Get(newId) != nullptr);
    CHECK(store.Notes().size() == 3);
}

// ── Export / import ────────────────────────────────────────────────────────────

static void test_export_all_import_roundtrip()
{
    Magpie::NotesStore src;
    src.Create("First").body  = "- [ ] task\n[&AgH1WQAA]";
    src.Create("Second").body = "# Head";
    const std::string file = src.ExportJson();

    Magpie::NotesStore dst;
    dst.Create("Existing");
    CHECK_EQ(dst.ImportJson(file), 2);
    CHECK(dst.Notes().size() == 3);
    CHECK_EQ(dst.Notes()[0].title, std::string("Existing"));   // appended, never replaced
    CHECK_EQ(dst.Notes()[1].title, std::string("First"));
    CHECK_EQ(dst.Notes()[1].body,  std::string("- [ ] task\n[&AgH1WQAA]"));
    CHECK_EQ(dst.Notes()[2].title, std::string("Second"));
}

static void test_export_selected_ids_only()
{
    Magpie::NotesStore src;
    src.Create("A");
    const std::string idB = src.Create("B").id;
    src.Create("C");
    Magpie::NotesStore dst;
    CHECK_EQ(dst.ImportJson(src.ExportJson({idB, "missing"})), 1);
    CHECK_EQ(dst.Notes()[0].title, std::string("B"));
}

static void test_reimport_same_note_is_skipped()
{
    Magpie::NotesStore src;
    src.Create("A");
    const std::string file = src.ExportJson();
    Magpie::NotesStore dst;
    CHECK_EQ(dst.ImportJson(file), 1);
    CHECK_EQ(dst.ImportJson(file), 0);     // same id already present
    CHECK(dst.Notes().size() == 1);
    CHECK_EQ(dst.Notes()[0].id, src.Notes()[0].id);   // id travels with the note
    // Even after editing locally, the note stays "the same note".
    dst.Get(src.Notes()[0].id)->body = "changed";
    CHECK_EQ(dst.ImportJson(file), 0);
}

static void test_import_into_source_store_adds_nothing()
{
    Magpie::NotesStore store;
    store.Create("A");
    store.Create("B");
    CHECK_EQ(store.ImportJson(store.ExportJson()), 0);
    CHECK(store.Notes().size() == 2);
}

static void test_prepare_import_flags_duplicates()
{
    Magpie::NotesStore src;
    const std::string idA = src.Create("A").id;
    src.Create("B");
    Magpie::NotesStore dst;
    dst.ImportJson(src.ExportJson({idA}));

    std::vector<Magpie::ImportItem> items;
    CHECK(dst.PrepareImport(src.ExportJson(), items));
    CHECK(items.size() == 2);
    CHECK(items[0].duplicate);       // A already imported
    CHECK(!items[1].duplicate);
    CHECK(dst.Notes().size() == 1);  // prepare never changes the store

    // Import only the selected (non-duplicate) item.
    CHECK_EQ(dst.AddImported({items[1]}), 1);
    CHECK_EQ(dst.Notes()[1].title, std::string("B"));
}

static void test_changed_duplicate_can_be_updated()
{
    Magpie::NotesStore src;
    const std::string id = src.Create("Daily").id;
    src.Get(id)->body = "- [ ] old";
    Magpie::NotesStore dst;
    dst.ImportJson(src.ExportJson());

    std::vector<Magpie::ImportItem> items;
    CHECK(dst.PrepareImport(src.ExportJson(), items));
    CHECK(items[0].duplicate);
    CHECK(!items[0].changed);                 // identical -> nothing to update

    src.Get(id)->body  = "- [ ] new";
    src.Get(id)->title = "Daily v2";
    CHECK(dst.PrepareImport(src.ExportJson(), items));
    CHECK(items[0].duplicate);
    CHECK(items[0].changed);
    CHECK_EQ(dst.AddImported(items), 0);      // never added as a second copy
    CHECK_EQ(dst.Notes()[0].body, std::string("- [ ] old"));   // untouched until asked

    CHECK_EQ(dst.UpdateImported(items), 1);
    CHECK(dst.Notes().size() == 1);
    CHECK_EQ(dst.Notes()[0].id, id);
    CHECK_EQ(dst.Notes()[0].title, std::string("Daily v2"));
    CHECK_EQ(dst.Notes()[0].body, std::string("- [ ] new"));
    CHECK_EQ(dst.UpdateImported(items), 0);   // already up to date
}

static void test_update_ignores_unknown_and_idless_items()
{
    Magpie::NotesStore dst;
    dst.Create("A");
    Magpie::ImportItem unknown;  unknown.id = std::string(32, 'b'); unknown.title = "X";
    Magpie::ImportItem idless;   idless.title = "A"; idless.body = "changed";
    CHECK_EQ(dst.UpdateImported({unknown, idless}), 0);
    CHECK(dst.Notes().size() == 1);
    CHECK_EQ(dst.Notes()[0].body, std::string(""));
}

static void test_prepare_import_flags_repeats_within_file()
{
    Magpie::NotesStore dst;
    std::vector<Magpie::ImportItem> items;
    const std::string id = std::string(32, 'a');
    CHECK(dst.PrepareImport(
        "[{\"id\":\"" + id + "\",\"title\":\"A\",\"body\":\"\"},"
        " {\"id\":\"" + id + "\",\"title\":\"A2\",\"body\":\"\"}]", items));
    CHECK(items.size() == 2);
    CHECK(!items[0].duplicate);
    CHECK(items[1].duplicate);
}

static void test_ids_are_unique_format()
{
    Magpie::NotesStore store;
    const std::string id = store.Create("A").id;
    CHECK(Magpie::IsNoteId(id));
    CHECK(!Magpie::IsNoteId("00000000"));
    CHECK(!Magpie::IsNoteId(std::string(32, 'G')));
}

static void test_fromjson_migrates_legacy_ids()
{
    Magpie::NotesStore store;
    const std::string keep = std::string(31, '0') + "1";
    CHECK(store.FromJson(
        "[{\"id\":\"00000000\",\"title\":\"Old\",\"body\":\"\"},"
        " {\"id\":\"" + keep + "\",\"title\":\"New\",\"body\":\"\"},"
        " {\"id\":\"" + keep + "\",\"title\":\"Dup\",\"body\":\"\"}]"));
    CHECK(Magpie::IsNoteId(store.Notes()[0].id));
    CHECK_EQ(store.Notes()[1].id, keep);             // valid ids are kept
    CHECK(store.Notes()[2].id != keep);              // repeats get a new id
    CHECK(Magpie::IsNoteId(store.Notes()[2].id));
}

static void test_import_plain_notes_json_array()
{
    // A Trailpack / Magpie Memo notes.json: ids are ignored, notes appended.
    Magpie::NotesStore dst;
    dst.Create("Mine");
    const std::string legacy = "[{\"id\":\"00000000\",\"title\":\"Old\",\"body\":\"x\"}]";
    CHECK_EQ(dst.ImportJson(legacy), 1);
    CHECK(dst.Notes().size() == 2);
    CHECK(Magpie::IsNoteId(dst.Notes()[1].id));      // legacy id replaced
    CHECK_EQ(dst.Notes()[1].body, std::string("x"));
    // No usable id -> identical title + body counts as a duplicate.
    CHECK_EQ(dst.ImportJson(legacy), 0);
}

static void test_import_invalid_leaves_store_untouched()
{
    Magpie::NotesStore dst;
    dst.Create("Keep");
    CHECK_EQ(dst.ImportJson("not json"), -1);
    CHECK_EQ(dst.ImportJson("{\"format\":\"other\",\"notes\":[]}"), -1);
    CHECK_EQ(dst.ImportJson("{\"notes\":[{\"title\":\"a\",\"body\":\"b\"}]}"), -1);
    // One bad entry rejects the whole file (all-or-nothing).
    CHECK_EQ(dst.ImportJson("[{\"title\":\"ok\",\"body\":\"\"},{\"title\":1,\"body\":\"\"}]"), -1);
    CHECK(dst.Notes().size() == 1);
}

// ── Groups ─────────────────────────────────────────────────────────────────────

static void test_group_create_move_delete()
{
    Magpie::NotesStore store;
    Magpie::Group& g = store.CreateGroup("Dailies");
    const std::string gid = g.id;
    CHECK(Magpie::IsNoteId(gid));
    const std::string a = store.Create("A", gid).id;
    const std::string b = store.Create("B").id;
    CHECK_EQ(store.Get(a)->groupId, gid);
    CHECK(store.Get(b)->groupId.empty());

    CHECK(store.MoveToGroup(b, gid));
    CHECK_EQ(store.Get(b)->groupId, gid);
    CHECK(!store.MoveToGroup(b, std::string(32, 'c')));   // unknown group
    CHECK(store.MoveToGroup(b, ""));                        // ungroup
    CHECK(store.Get(b)->groupId.empty());

    // Create with an unknown group id -> ungrouped.
    CHECK(store.Create("C", std::string(32, 'd')).groupId.empty());

    store.DeleteGroup(gid);
    CHECK(store.Groups().empty());
    CHECK(store.Notes().size() == 3);                       // notes are kept
    CHECK(store.Get(a)->groupId.empty());
}

static void test_groups_persist_roundtrip()
{
    Magpie::NotesStore src;
    Magpie::Group& g = src.CreateGroup("Raids");
    g.collapsed = true;
    const std::string gid = g.id;
    src.Create("W1", gid);
    src.Create("Loose");

    Magpie::NotesStore dst;
    CHECK(dst.FromJson(src.ToJson()));
    CHECK(dst.Groups().size() == 1);
    CHECK_EQ(dst.Groups()[0].id, gid);
    CHECK_EQ(dst.Groups()[0].name, std::string("Raids"));
    CHECK(dst.Groups()[0].collapsed);
    CHECK_EQ(dst.Notes()[0].groupId, gid);
    CHECK(dst.Notes()[1].groupId.empty());
}

static void test_fromjson_drops_unknown_group_refs()
{
    Magpie::NotesStore store;
    const std::string nid = std::string(31, '0') + "1";
    CHECK(store.FromJson(
        "{\"format\":\"trailpack-memo-notes\",\"version\":2,\"groups\":[],"
        "\"notes\":[{\"id\":\"" + nid + "\",\"title\":\"A\",\"body\":\"\","
        "\"group\":\"" + std::string(32, 'e') + "\"}]}"));
    CHECK(store.Notes()[0].groupId.empty());
}

static void test_export_all_includes_groups_and_import_recreates_them()
{
    Magpie::NotesStore src;
    const std::string gid = src.CreateGroup("Dailies").id;
    src.CreateGroup("Empty");
    src.Create("A", gid);
    src.Create("B");

    Magpie::NotesStore dst;
    CHECK_EQ(dst.ImportJson(src.ExportJson()), 2);
    CHECK(dst.Groups().size() == 1);               // only groups used by imported notes
    CHECK_EQ(dst.Groups()[0].id, gid);              // group keeps its id
    CHECK_EQ(dst.Groups()[0].name, std::string("Dailies"));
    CHECK_EQ(dst.Notes()[0].groupId, gid);
    CHECK(dst.Notes()[1].groupId.empty());
}

static void test_import_reuses_existing_group_by_id()
{
    Magpie::NotesStore src;
    const std::string gid = src.CreateGroup("Dailies").id;
    src.Create("A", gid);
    const std::string file = src.ExportJson();
    src.Create("B", gid);

    Magpie::NotesStore dst;
    dst.ImportJson(file);
    dst.GetGroup(gid)->name = "Renamed locally";
    CHECK_EQ(dst.ImportJson(src.ExportJson()), 1);  // only B is new
    CHECK(dst.Groups().size() == 1);                // no second "Dailies"
    CHECK_EQ(dst.Groups()[0].name, std::string("Renamed locally"));
    CHECK_EQ(dst.Notes()[1].groupId, gid);
}

static void test_export_single_note_and_group()
{
    Magpie::NotesStore src;
    const std::string g1 = src.CreateGroup("G1").id;
    const std::string g2 = src.CreateGroup("G2").id;
    const std::string a  = src.Create("A", g1).id;
    src.Create("B", g1);
    src.Create("C", g2);

    std::vector<Magpie::ImportItem> items;
    Magpie::NotesStore probe;
    CHECK(probe.PrepareImport(src.ExportJson({a}), items));
    CHECK(items.size() == 1);
    CHECK_EQ(items[0].groupId, g1);                 // the note's group travels along
    CHECK_EQ(items[0].groupName, std::string("G1"));

    CHECK(probe.PrepareImport(src.ExportGroupJson(g1), items));
    CHECK(items.size() == 2);
    CHECK_EQ(items[0].title, std::string("A"));
    CHECK_EQ(items[1].title, std::string("B"));
}

static void test_import_v1_export_without_groups()
{
    Magpie::NotesStore dst;
    CHECK_EQ(dst.ImportJson("{\"format\":\"trailpack-memo-export\",\"version\":1,"
                            "\"notes\":[{\"id\":\"" + std::string(32, 'a') + "\","
                            "\"title\":\"Old\",\"body\":\"x\"}]}"), 1);
    CHECK(dst.Groups().empty());
    CHECK(dst.Notes()[0].groupId.empty());
}

static void test_group_color_persists_and_travels()
{
    Magpie::NotesStore src;
    Magpie::Group& g = src.CreateGroup("Coloured");
    g.hasColor = true;
    g.color[0] = 1.0f; g.color[1] = 0.5f; g.color[2] = 0.0f; g.color[3] = 1.0f;
    const std::string gid = g.id;
    src.Create("A", gid);
    src.CreateGroup("Plain");

    Magpie::NotesStore loaded;
    CHECK(loaded.FromJson(src.ToJson()));
    CHECK(loaded.GetGroup(gid)->hasColor);
    CHECK(loaded.GetGroup(gid)->color[1] == 0.5f);
    CHECK(!loaded.Groups()[1].hasColor);

    Magpie::NotesStore imported;
    CHECK_EQ(imported.ImportJson(src.ExportJson()), 1);
    CHECK(imported.GetGroup(gid)->hasColor);           // new group takes the file's colour
    CHECK(imported.GetGroup(gid)->color[0] == 1.0f);

    // An existing group keeps its local colour.
    Magpie::NotesStore local;
    local.ImportJson(src.ExportJson());
    local.GetGroup(gid)->hasColor = false;
    src.Create("B", gid);
    local.ImportJson(src.ExportJson());
    CHECK(!local.GetGroup(gid)->hasColor);
}

// ── Reset times / types ────────────────────────────────────────────────────────

static void test_reset_times()
{
    using namespace Magpie::Reset;
    // 2026-10-29 (Thursday) 12:00:00 UTC
    const int64_t thu = 1793275200;
    CHECK_EQ(LastDaily(thu), (int64_t)1793232000);              // 2026-10-29 00:00 UTC
    CHECK_EQ(NextDaily(thu), (int64_t)1793318400);              // 2026-10-30 00:00 UTC
    CHECK_EQ(LastWeekly(thu), (int64_t)1792999800);             // Mon 2026-10-26 07:30 UTC
    CHECK_EQ(NextWeekly(thu), (int64_t)1793604600);             // Mon 2026-11-02 07:30 UTC
    // Exactly at a reset: that reset counts as "last".
    CHECK_EQ(LastWeekly(1792999800), (int64_t)1792999800);
    CHECK_EQ(LastDaily(1793232000), (int64_t)1793232000);
    // Monday before 07:30 UTC still belongs to the previous week.
    CHECK_EQ(LastWeekly(1792999800 - 60), (int64_t)(1792999800 - 7 * 86400));

    CHECK_EQ(FormatCountdown(30), std::string("<1m"));
    CHECK_EQ(FormatCountdown(12 * 60), std::string("12m"));
    CHECK_EQ(FormatCountdown(3 * 3600 + 12 * 60), std::string("3h 12m"));
    CHECK_EQ(FormatCountdown(2 * 86400 + 5 * 3600 + 59), std::string("2d 5h"));
}

static void test_daily_note_resets_after_reset_only()
{
    const int64_t thu = 1793275200;                 // Thu 12:00 UTC
    Magpie::NotesStore store;
    Magpie::Note& n = store.Create("Dailies");
    n.body = "- [x] a\n- [ ] b";
    const std::string id = n.id;
    CHECK(store.SetType(id, Magpie::NoteType::Daily, thu));

    CHECK_EQ(store.ApplyResets(thu + 3600), 0);      // same day: ticks stay
    CHECK_EQ(store.Get(id)->body, std::string("- [x] a\n- [ ] b"));

    CHECK_EQ(store.ApplyResets(thu + 13 * 3600), 1); // past 00:00 UTC
    CHECK_EQ(store.Get(id)->body, std::string("- [ ] a\n- [ ] b"));
    store.Get(id)->body = "- [x] a\n- [ ] b";
    CHECK_EQ(store.ApplyResets(thu + 14 * 3600), 0); // only once per reset

    // Several days offline -> still a single reset.
    CHECK_EQ(store.ApplyResets(thu + 5 * 86400), 1);
    CHECK_EQ(store.Get(id)->body, std::string("- [ ] a\n- [ ] b"));
}

static void test_weekly_and_plain_notes()
{
    const int64_t thu = 1793275200;
    Magpie::NotesStore store;
    Magpie::Note& w = store.Create("Weekly");
    w.body = "- [x] raid";
    const std::string wid = w.id;
    Magpie::Note& p = store.Create("Plain");
    p.body = "- [x] keep";
    const std::string pid = p.id;
    store.SetType(wid, Magpie::NoteType::Weekly, thu);

    CHECK_EQ(store.ApplyResets(thu + 2 * 86400), 0);         // Sat: no weekly reset yet
    CHECK_EQ(store.ApplyResets(1793604600 + 1), 1);          // after Mon 07:30 UTC
    CHECK_EQ(store.Get(wid)->body, std::string("- [ ] raid"));
    CHECK_EQ(store.Get(pid)->body, std::string("- [x] keep"));   // plain notes never reset
}

static void test_reset_skips_note_in_editor()
{
    const int64_t thu = 1793275200;
    Magpie::NotesStore store;
    Magpie::Note& n = store.Create("D");
    n.body = "- [x] a";
    const std::string id = n.id;
    store.SetType(id, Magpie::NoteType::Daily, thu);
    CHECK_EQ(store.ApplyResets(thu + 86400, id), 0);   // open in the editor
    CHECK_EQ(store.ApplyResets(thu + 86400), 1);        // reset once the edit ended
}

static void test_type_persists_and_travels()
{
    const int64_t thu = 1793275200;
    Magpie::NotesStore src;
    const std::string id = src.Create("D").id;
    src.SetType(id, Magpie::NoteType::Weekly, thu);

    Magpie::NotesStore loaded;
    CHECK(loaded.FromJson(src.ToJson()));
    CHECK(loaded.Get(id)->type == Magpie::NoteType::Weekly);
    CHECK_EQ(loaded.Get(id)->resetStamp, src.Get(id)->resetStamp);

    Magpie::NotesStore imported;
    imported.ImportJson(src.ExportJson());
    CHECK(imported.Get(id)->type == Magpie::NoteType::Weekly);
    CHECK_EQ(imported.Get(id)->resetStamp, src.Get(id)->resetStamp);

    CHECK(Magpie::NoteTypeFromKey("bogus") == Magpie::NoteType::Note);
}

static void test_delete_many()
{
    Magpie::NotesStore store;
    const std::string g = store.CreateGroup("G").id;
    const std::string a = store.Create("A", g).id;
    const std::string b = store.Create("B").id;
    const std::string c = store.Create("C").id;
    CHECK_EQ(store.DeleteMany({a, c, "unknown"}), 2);
    CHECK(store.Notes().size() == 1);
    CHECK_EQ(store.Notes()[0].id, b);
    CHECK(store.Groups().size() == 1);              // groups are left alone
    CHECK_EQ(store.DeleteMany({}), 0);
}

// ── I18n ───────────────────────────────────────────────────────────────────────

static void test_i18n_lookup_and_fallback()
{
    using namespace Magpie::I18n;
    SetLang(Lang::En);
    CHECK_EQ(std::string(Tr("Save")), std::string("Save"));
    SetLang(Lang::De);
    CHECK_EQ(std::string(Tr("Save")), std::string("Speichern"));
    CHECK_EQ(std::string(Tr("no such key")), std::string("no such key"));   // falls back to English
    CHECK_EQ(std::string(TrN(1, "Import 1 note", "Import %d notes")), std::string("1 Notiz importieren"));
    CHECK_EQ(std::string(TrN(3, "Import 1 note", "Import %d notes")), std::string("%d Notizen importieren"));
    SetLang(Lang::En);
}

// printf conversions of a format string, in order ("%%" ignored).
static std::vector<std::string> FormatSpecs(const char* f)
{
    std::vector<std::string> out;
    for (const char* p = f; *p; ++p) {
        if (*p != '%') continue;
        if (p[1] == '%') { ++p; continue; }
        std::string spec = "%";
        while (*++p && !std::isalpha((unsigned char)*p)) spec += *p;
        while (*p == 'l' || *p == 'z' || *p == 'h') spec += *p++;
        if (*p) spec += *p; else break;
        out.push_back(spec);
    }
    return out;
}

static void test_i18n_table_is_consistent()
{
    std::set<std::string> keys;
    for (const auto& e : Magpie::I18n::GermanTable()) {
        CHECK(e.first != nullptr && e.first[0] != '\0');
        CHECK(e.second != nullptr && e.second[0] != '\0');
        CHECK(keys.insert(e.first).second);                        // no duplicate keys
        CHECK(FormatSpecs(e.first) == FormatSpecs(e.second));      // same printf arguments
        if (FormatSpecs(e.first) != FormatSpecs(e.second))
            std::cerr << "  format mismatch: " << e.first << "\n";
    }
}

// ── Decoder Ring ABI header tests ──────────────────────────────────────────────

static void test_decoder_header_is_v4()
{
    // Pin the vendored ABI header to the current Decoder Ring release. If DR
    // ships a new header and we forget to re-vendor, this fails loudly. (The
    // runtime guard in DecoderClient accepts >= MIN_SUPPORTED for forward-compat;
    // this is the separate "is the header current" check.)
    CHECK(DECODER_RING_API_VERSION == 4u);
}

// ── ChatLinks codec tests ─────────────────────────────────────────────────────

static void test_codec_decode_item_extracts_upgrades()
{
    using namespace PieUI::ChatLinks;
    // A geared item (weapon) with two upgrade slots filled (rune/sigil ids).
    // Step 3 reads these to show the upgrade's set bonuses on the item tooltip.
    std::string link = EncodeItem(46774, 1, 0, 24615, 24618);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_ITEM);
    DecodedItemLink di;
    CHECK(DecodeItem(link, di));
    CHECK(di.item_id == 46774u);
    CHECK(di.upgrade1_id == 24615u);
    CHECK(di.upgrade2_id == 24618u);
}

static void test_codec_decode_item_no_upgrades_is_zero()
{
    using namespace PieUI::ChatLinks;
    // A plain item has no upgrades -> both ids must be 0 (Step 3 skips them).
    std::string link = EncodeItem(19684 /* Copper Ore */, 1);
    DecodedItemLink di;
    CHECK(DecodeItem(link, di));
    CHECK(di.upgrade1_id == 0u);
    CHECK(di.upgrade2_id == 0u);
}

static void test_codec_encode_item_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeItem(46774);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_ITEM);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_ITEM)), std::string("[Item]"));
}

static void test_codec_encode_map_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeMap(900); // arbitrary PoI id
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_MAP);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_MAP)), std::string("[Waypoint]"));
}

static void test_codec_encode_skill_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkill(5516);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_SKILL);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_SKILL)), std::string("[Skill]"));
}

static void test_codec_encode_skin_roundtrip()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkin(8585);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_SKIN);
    CHECK_EQ(std::string(LinkTypeLabel(LINK_SKIN)), std::string("[Skin]"));
}

static void test_codec_segment_line_plain_and_link()
{
    using namespace PieUI::ChatLinks;
    // Build an item link and embed it in a text line
    std::string itemLink = EncodeItem(46774);
    std::string line = "Check out " + itemLink + " here";
    auto segs = SegmentLine(line);
    // Expect at least 3 segments: plain, link, plain
    CHECK(segs.size() >= 3);
    // Find the link segment
    bool foundLink = false;
    for (const auto& s : segs) {
        if (s.kind == SegmentKind::Link) {
            CHECK(s.linkType == LINK_ITEM);
            CHECK_EQ(s.raw, itemLink);
            foundLink = true;
        }
    }
    CHECK(foundLink);
}

static void test_codec_segment_line_plain_only()
{
    using namespace PieUI::ChatLinks;
    auto segs = SegmentLine("just plain text");
    CHECK(segs.size() == 1);
    CHECK(segs[0].kind == SegmentKind::Plain);
    CHECK_EQ(segs[0].display, std::string("just plain text"));
}

static void test_codec_link_type_none_for_garbage()
{
    using namespace PieUI::ChatLinks;
    CHECK(DetectType("not a link") == LINK_NONE);
    CHECK(DetectType("") == LINK_NONE);
}

static void test_specdata_elite_spec_name()
{
    using namespace PieUI::SpecData;
    // Known HoT elite specs
    CHECK_EQ(std::string(GetEliteSpecName(5)),  std::string("Druid"));
    CHECK_EQ(std::string(GetEliteSpecName(7)),  std::string("Daredevil"));
    CHECK_EQ(std::string(GetEliteSpecName(40)), std::string("Chronomancer"));
    // Known PoF elite specs
    CHECK_EQ(std::string(GetEliteSpecName(59)), std::string("Mirage"));
    CHECK_EQ(std::string(GetEliteSpecName(62)), std::string("Firebrand"));
    // Known EoD elite specs
    CHECK_EQ(std::string(GetEliteSpecName(70)), std::string("Mechanist"));
    CHECK_EQ(std::string(GetEliteSpecName(72)), std::string("Untamed"));
    // Unknown returns nullptr
    CHECK(GetEliteSpecName(0) == nullptr);
    CHECK(GetEliteSpecName(999) == nullptr);
}

static void test_specdata_profession_name()
{
    using namespace PieUI::SpecData;
    CHECK_EQ(std::string(GetProfessionName(1)), std::string("Guardian"));
    CHECK_EQ(std::string(GetProfessionName(2)), std::string("Warrior"));
    CHECK_EQ(std::string(GetProfessionName(7)), std::string("Mesmer"));
    CHECK_EQ(std::string(GetProfessionName(9)), std::string("Revenant"));
    // Unknown returns empty string (not nullptr)
    CHECK_EQ(std::string(GetProfessionName(0)),   std::string(""));
    CHECK_EQ(std::string(GetProfessionName(99)),  std::string(""));
}

// ── Markdown parser tests ─────────────────────────────────────────────────────

static void test_md_heading_level1()
{
    auto lines = Magpie::Md::Parse("# Hello");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 1);
    CHECK(lines[0].spans.size() == 1);
    CHECK(lines[0].spans[0].kind == Magpie::Md::Inline::Text);
    CHECK_EQ(lines[0].spans[0].text, std::string("Hello"));
}

static void test_md_heading_level3()
{
    auto lines = Magpie::Md::Parse("### SubHead");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 3);
    CHECK(lines[0].spans.size() == 1);
    CHECK_EQ(lines[0].spans[0].text, std::string("SubHead"));
}

static void test_md_bullet()
{
    auto lines = Magpie::Md::Parse("- item text");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Bullet);
    CHECK(lines[0].headingLevel == 0);
    // spans should contain the item text
    bool foundText = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("item text") != std::string::npos) {
            foundText = true;
        }
    }
    CHECK(foundText);
}

static void test_md_paragraph()
{
    auto lines = Magpie::Md::Parse("just a plain line");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Paragraph);
    CHECK(lines[0].headingLevel == 0);
    CHECK(lines[0].spans.size() >= 1);
    // Collect all text
    std::string all;
    for (const auto& s : lines[0].spans) all += s.text;
    CHECK_EQ(all, std::string("just a plain line"));
}

static void test_md_bold()
{
    auto lines = Magpie::Md::Parse("a **b** c");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Paragraph);
    // Expect spans: Text "a ", Bold "b", Text " c" (possibly adjacent texts merged)
    bool foundBold = false;
    std::string fullText;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold) {
            CHECK_EQ(s.text, std::string("b"));
            foundBold = true;
        }
        if (s.kind == Magpie::Md::Inline::Text) {
            fullText += s.text;
        }
    }
    CHECK(foundBold);
    // The surrounding text should be present
    CHECK(fullText.find("a ") != std::string::npos);
    CHECK(fullText.find(" c") != std::string::npos);
}

static void test_md_italic()
{
    auto lines = Magpie::Md::Parse("*i*");
    CHECK(lines.size() == 1);
    bool foundItalic = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Italic) {
            CHECK_EQ(s.text, std::string("i"));
            foundItalic = true;
        }
    }
    CHECK(foundItalic);
}

static void test_md_chip_inline()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(46774);  // e.g. "[&AgF2tgAA]"
    std::string body = "see " + code + " there";
    auto lines = Magpie::Md::Parse(body);
    CHECK(lines.size() == 1);
    // Find chip span with the raw code
    bool foundChip = false;
    bool foundBefore = false;
    bool foundAfter  = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Chip) {
            CHECK_EQ(s.text, code);
            foundChip = true;
        }
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("see") != std::string::npos) {
            foundBefore = true;
        }
        if (s.kind == Magpie::Md::Inline::Text && s.text.find("there") != std::string::npos) {
            foundAfter = true;
        }
    }
    CHECK(foundChip);
    CHECK(foundBefore);
    CHECK(foundAfter);
}

static void test_md_bold_with_chip()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(1234);
    std::string body = "**bold** " + code + " end";
    auto lines = Magpie::Md::Parse(body);
    CHECK(lines.size() == 1);
    bool foundBold = false;
    bool foundChip = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold)  { CHECK_EQ(s.text, std::string("bold")); foundBold = true; }
        if (s.kind == Magpie::Md::Inline::Chip)  { CHECK_EQ(s.text, code); foundChip = true; }
    }
    CHECK(foundBold);
    CHECK(foundChip);
}

static void test_md_unmatched_star_is_literal()
{
    auto lines = Magpie::Md::Parse("hello *world");
    CHECK(lines.size() == 1);
    // The unmatched * must appear literally in the output text
    std::string all;
    for (const auto& s : lines[0].spans) all += s.text;
    CHECK(all.find('*') != std::string::npos);
    CHECK(all.find("world") != std::string::npos);
}

static void test_md_multiline()
{
    auto lines = Magpie::Md::Parse("line one\nline two\nline three");
    CHECK(lines.size() == 3);
    // Each line should contain its own text
    std::string t0, t1, t2;
    for (const auto& s : lines[0].spans) t0 += s.text;
    for (const auto& s : lines[1].spans) t1 += s.text;
    for (const auto& s : lines[2].spans) t2 += s.text;
    CHECK(t0.find("line one") != std::string::npos);
    CHECK(t1.find("line two") != std::string::npos);
    CHECK(t2.find("line three") != std::string::npos);
}

static void test_md_heading_with_bold()
{
    auto lines = Magpie::Md::Parse("## **title**");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Heading);
    CHECK(lines[0].headingLevel == 2);
    bool foundBold = false;
    for (const auto& s : lines[0].spans) {
        if (s.kind == Magpie::Md::Inline::Bold) { CHECK_EQ(s.text, std::string("title")); foundBold = true; }
    }
    CHECK(foundBold);
}

// ── ChipResolver tests ────────────────────────────────────────────────────────

static void test_chip_item_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeItem(46774);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Item]"));
    CHECK(v.color != 0);
}

static void test_chip_waypoint_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeMap(900);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Waypoint]"));
    CHECK(v.color != 0);
}

static void test_chip_skill_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkill(5516);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Skill]"));
}

static void test_chip_skin_label()
{
    using namespace PieUI::ChatLinks;
    std::string link = EncodeSkin(8585);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Skin]"));
}

static void test_chip_garbage_fallback()
{
    // Garbage input must not crash and must return the safe fallback label.
    Magpie::ChipView v = Magpie::ResolveChip("not a link");
    CHECK_EQ(v.label, std::string("[link]"));
    Magpie::ChipView v2 = Magpie::ResolveChip("");
    CHECK_EQ(v2.label, std::string("[link]"));
}

static void test_chip_colours_distinct()
{
    // Item and Waypoint must have distinct, non-zero colours.
    using namespace PieUI::ChatLinks;
    Magpie::ChipView item = Magpie::ResolveChip(EncodeItem(1));
    Magpie::ChipView wp   = Magpie::ResolveChip(EncodeMap(1));
    CHECK(item.color != 0);
    CHECK(wp.color != 0);
    CHECK(item.color != wp.color);
}

static void test_chip_non_build_never_returns_build_path()
{
    // Skill, skin, waypoint must never produce a "[... Build]" label.
    using namespace PieUI::ChatLinks;
    Magpie::ChipView skill = Magpie::ResolveChip(EncodeSkill(5516));
    Magpie::ChipView skin  = Magpie::ResolveChip(EncodeSkin(8585));
    Magpie::ChipView wp    = Magpie::ResolveChip(EncodeMap(900));
    CHECK(skill.label.find("Build") == std::string::npos);
    CHECK(skin.label.find("Build") == std::string::npos);
    CHECK(wp.label.find("Build") == std::string::npos);
}

static void test_chip_build_label_via_spec_helpers()
{
    // We can't easily synthesize a valid build chat code without a full encoder
    // round-trip, but we CAN verify the spec-label mapping that BuildLabel uses:
    // GetEliteSpecName(59) == "Mirage"  → would produce "[Mirage Build]"
    // GetProfessionName(7) == "Mesmer"  → fallback would produce "[Mesmer Build]"
    // This covers the label-derivation logic even without a live build code.
    const char* mirage = PieUI::SpecData::GetEliteSpecName(59);
    CHECK(mirage != nullptr);
    CHECK_EQ(std::string(mirage), std::string("Mirage"));
    std::string expectedLabel = std::string("[") + mirage + " Build]";
    CHECK_EQ(expectedLabel, std::string("[Mirage Build]"));

    const char* mesmer = PieUI::SpecData::GetProfessionName(7);
    CHECK(mesmer != nullptr);
    CHECK_EQ(std::string(mesmer), std::string("Mesmer"));
    std::string profLabel = std::string("[") + mesmer + " Build]";
    CHECK_EQ(profLabel, std::string("[Mesmer Build]"));
}

static void test_chip_build_roundtrip_via_encode()
{
    // Construct a minimal Mirage (Mesmer elite-spec 59) build link using EncodeBuild
    // and verify that ResolveChip produces "[Mirage Build]".
    using namespace PieUI::ChatLinks;
    DecodedBuildLink b{};
    b.profession = 7; // Mesmer
    // spec slot 0 = Mirage (spec id 59)
    b.specs[0].spec_id = 59;
    b.specs[1].spec_id = 0;
    b.specs[2].spec_id = 0;
    std::string link = EncodeBuild(b);
    CHECK(!link.empty());
    CHECK(DetectType(link) == LINK_BUILD);
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Mirage Build]"));
    CHECK(v.color != 0);
}

static void test_chip_build_core_profession_fallback()
{
    // A build link with no elite specs → should fall back to "[Mesmer Build]".
    using namespace PieUI::ChatLinks;
    DecodedBuildLink b{};
    b.profession = 7; // Mesmer
    // All spec slots are 0 (core / no spec)
    std::string link = EncodeBuild(b);
    CHECK(!link.empty());
    Magpie::ChipView v = Magpie::ResolveChip(link);
    CHECK_EQ(v.label, std::string("[Mesmer Build]"));
}

// ── ChipCodec round-trip tests ────────────────────────────────────────────────

static void test_chipcodec_roundtrip_plain()
{
    std::string s = "hello world";
    CHECK_EQ(Magpie::CellsToText(Magpie::ParseToCells(s)), s);
}

static void test_chipcodec_roundtrip_with_chip()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(19684 /* Copper Ore */, 1);
    CHECK(!code.empty());
    std::string s = "got " + code + " here";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    // The chip is exactly ONE cell, isChip, code == the embedded code.
    int chipCount = 0; std::string chipCode;
    for (const auto& c : cells) if (c.isChip) { ++chipCount; chipCode = c.code; }
    CHECK(chipCount == 1);
    CHECK_EQ(chipCode, code);
}

static void test_chipcodec_roundtrip_multiline()
{
    std::string s = "line one\nline two\nthird";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    // A '\n' becomes a text cell with cp == '\n'.
    int nlCount = 0;
    for (const auto& c : cells) if (!c.isChip && c.cp == '\n') ++nlCount;
    CHECK(nlCount == 2);
}

static void test_chipcodec_roundtrip_mixed()
{
    using namespace PieUI::ChatLinks;
    std::string code = EncodeItem(19684, 1);
    CHECK(!code.empty());
    std::string s = "top " + code + "\nmid line\n" + code + " end";
    auto cells = Magpie::ParseToCells(s);
    CHECK_EQ(Magpie::CellsToText(cells), s);
    int chipCount = 0, nlCount = 0;
    for (const auto& c : cells) {
        if (c.isChip) ++chipCount;
        else if (c.cp == '\n') ++nlCount;
    }
    CHECK(chipCount == 2);
    CHECK(nlCount == 2);
}

// ── IconUrl tests ─────────────────────────────────────────────────────────────

static void test_iconurl_key_stable()
{
    // Same URL hashes to the same value every call
    uint32_t a = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    uint32_t b = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    CHECK(a == b);
}

static void test_iconurl_key_differs()
{
    // Different URLs produce different hashes
    uint32_t a = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/AB/12.png");
    uint32_t b = Magpie::Icons::UrlKey("https://render.guildwars2.com/file/CD/99.png");
    CHECK(a != b);
}

static void test_iconurl_key_null_does_not_crash()
{
    // Null input must not crash; we just check it runs
    (void)Magpie::Icons::UrlKey(nullptr);
    (void)Magpie::Icons::UrlKey("");
}

static void test_iconurl_split_full_url()
{
    auto s = Magpie::Icons::SplitIconUrl("https://render.guildwars2.com/file/AB/12.png");
    CHECK_EQ(s.remote,   std::string("https://render.guildwars2.com"));
    CHECK_EQ(s.endpoint, std::string("/file/AB/12.png"));
}

static void test_iconurl_split_no_path()
{
    // URL with no path after the host — endpoint should be "/"
    auto s = Magpie::Icons::SplitIconUrl("https://render.guildwars2.com");
    CHECK_EQ(s.endpoint, std::string("/"));
}

static void test_iconurl_split_malformed()
{
    // No "://" — whole string goes into remote, endpoint = "/"
    auto s = Magpie::Icons::SplitIconUrl("not-a-real-url");
    CHECK_EQ(s.remote,   std::string("not-a-real-url"));
    CHECK_EQ(s.endpoint, std::string("/"));
}

static void test_iconurl_split_empty_does_not_crash()
{
    auto s = Magpie::Icons::SplitIconUrl("");
    CHECK_EQ(s.endpoint, std::string("/"));
}

// ── ChipColor tests ─────────────────────────────────────────────────────────────

// Mirror of the packing in ChipColor.cpp (IM_COL32 ABGR, alpha 255).
static uint32_t packRGB(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xFFu) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

static void test_chipcolor_item_rarity_palette()
{
    using namespace Magpie;
    // Exact Pie UI RarityColor values (float->255).
    CHECK(ChipColorForItemRarity(DR_Junk)       == packRGB(171, 171, 171));
    CHECK(ChipColorForItemRarity(DR_Basic)      == packRGB(255, 255, 255));
    CHECK(ChipColorForItemRarity(DR_Fine)       == packRGB( 97, 163, 217));
    CHECK(ChipColorForItemRarity(DR_Masterwork) == packRGB( 31, 214,   0));
    CHECK(ChipColorForItemRarity(DR_Rare)       == packRGB(255, 214,   0));
    CHECK(ChipColorForItemRarity(DR_Exotic)     == packRGB(255, 171,   0));
    CHECK(ChipColorForItemRarity(DR_Ascended)   == packRGB(250,  61, 140));
    CHECK(ChipColorForItemRarity(DR_Legendary)  == packRGB(163,  54, 237));
}

static void test_chipcolor_item_unknown_is_basic()
{
    using namespace Magpie;
    // Unresolved / absent service -> Basic (white), never a tier.
    CHECK(ChipColorForItemRarity(DR_RarityUnknown) == packRGB(255, 255, 255));
    CHECK(ChipColorForItemRarity(DR_RarityUnknown) == ChipColorForItemRarity(DR_Basic));
}

static void test_chipcolor_nonitem_types_are_blue()
{
    using namespace Magpie;
    using namespace PieUI::ChatLinks;
    const uint32_t blue = packRGB(120, 200, 255);
    CHECK(ChipColorForType(LINK_MAP)   == blue);   // waypoint
    CHECK(ChipColorForType(LINK_SKILL) == blue);
    CHECK(ChipColorForType(LINK_BUILD) == blue);
    CHECK(ChipColorForType(LINK_SKIN)  == blue);
}

static void test_chipcolor_combined_rule()
{
    using namespace Magpie;
    using namespace PieUI::ChatLinks;
    // Item -> rarity colour; waypoint -> blue regardless of the rarity arg.
    CHECK(ChipColor(LINK_ITEM, DR_Exotic) == packRGB(255, 171, 0));
    CHECK(ChipColor(LINK_ITEM, DR_RarityUnknown) == packRGB(255, 255, 255)); // Basic default
    CHECK(ChipColor(LINK_MAP, DR_Exotic) == packRGB(120, 200, 255));         // type wins for non-items
    CHECK(ChipColor(LINK_MAP, DR_RarityUnknown) == packRGB(120, 200, 255));
}

// ── Main ──────────────────────────────────────────────────────────────────────

// ── Task lists ────────────────────────────────────────────────────────────────

static void test_md_task_unchecked()
{
    auto lines = Magpie::Md::Parse("- [ ] buy ore");
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Task);
    CHECK(!lines[0].checked);
    CHECK(lines[0].spans.size() == 1);
    CHECK_EQ(lines[0].spans[0].text, std::string("buy ore"));
}

static void test_md_task_checked_both_cases()
{
    auto lines = Magpie::Md::Parse("- [x] a\n- [X] b\n  - [ ] indented");
    CHECK(lines.size() == 3);
    CHECK(lines[0].block == Magpie::Md::Block::Task && lines[0].checked);
    CHECK(lines[1].block == Magpie::Md::Block::Task && lines[1].checked);
    CHECK(lines[2].block == Magpie::Md::Block::Task && !lines[2].checked);
    CHECK_EQ(lines[2].spans[0].text, std::string("indented"));
}

static void test_md_task_empty_item_and_non_tasks()
{
    auto lines = Magpie::Md::Parse("- [ ]\n- [ ]x\n- [y] no\n- plain\n[ ] bare");
    CHECK(lines.size() == 5);
    CHECK(lines[0].block == Magpie::Md::Block::Task);       // marker at end of line
    CHECK(lines[0].spans.empty());
    CHECK(lines[1].block == Magpie::Md::Block::Bullet);     // no space after ']'
    CHECK(lines[2].block == Magpie::Md::Block::Bullet);     // invalid state char
    CHECK(lines[3].block == Magpie::Md::Block::Bullet);
    CHECK(lines[4].block == Magpie::Md::Block::Paragraph);  // GFM needs the dash
}

static void test_md_task_with_bold_and_chip()
{
    const std::string code = PieUI::ChatLinks::EncodeItem(46774);
    auto lines = Magpie::Md::Parse("- [x] **go** " + code);
    CHECK(lines.size() == 1);
    CHECK(lines[0].block == Magpie::Md::Block::Task && lines[0].checked);
    CHECK(lines[0].spans.size() >= 2);
    CHECK(lines[0].spans[0].kind == Magpie::Md::Inline::Bold);
    CHECK(lines[0].spans.back().kind == Magpie::Md::Inline::Chip);
}

static void test_md_count_tasks()
{
    auto tc = Magpie::Md::CountTasks("# List\n- [x] a\n- [ ] b\n- c\n- [X] d");
    CHECK_EQ(tc.total, 3);
    CHECK_EQ(tc.done, 2);
    auto none = Magpie::Md::CountTasks("");
    CHECK_EQ(none.total, 0);
}

static void test_md_toggle_task()
{
    std::string body = "intro\n- [ ] a\n  - [x] b";
    CHECK(Magpie::Md::ToggleTask(body, 1));
    CHECK_EQ(body, std::string("intro\n- [x] a\n  - [x] b"));
    CHECK(Magpie::Md::ToggleTask(body, 2));
    CHECK_EQ(body, std::string("intro\n- [x] a\n  - [ ] b"));
    CHECK(!Magpie::Md::ToggleTask(body, 0));    // not a task
    CHECK(!Magpie::Md::ToggleTask(body, 9));    // out of range
    CHECK(!Magpie::Md::ToggleTask(body, -1));
    CHECK_EQ(body, std::string("intro\n- [x] a\n  - [ ] b"));
}

static void test_md_uncheck_all()
{
    std::string body = "- [x] a\n- [ ] b\n- [X] c\ntext [x]";
    CHECK_EQ(Magpie::Md::UncheckAllTasks(body), 2);
    CHECK_EQ(body, std::string("- [ ] a\n- [ ] b\n- [ ] c\ntext [x]"));
    CHECK_EQ(Magpie::Md::UncheckAllTasks(body), 0);
}

// ── Markdown editing commands ─────────────────────────────────────────────────

static std::string RunEdit(const std::string& text, const Magpie::Md::EditResult& r)
{
    Magpie::Md::Cells c = Magpie::Md::ToCells(text);
    Magpie::Md::ApplyOps(c, r.ops);
    return Magpie::Md::FromCells(c);
}

static void test_edit_wrap_bold_selection()
{
    using namespace Magpie::Md;
    const std::string t = "say hello now";
    auto r = WrapSelection(ToCells(t), 4, 9, "**");
    CHECK(r.handled);
    CHECK_EQ(RunEdit(t, r), std::string("say **hello** now"));
    CHECK_EQ(r.selStart, 6);
    CHECK_EQ(r.selEnd, 11);
    // Pressing again with the same (inner) selection removes the markers.
    const std::string t2 = RunEdit(t, r);
    auto r2 = WrapSelection(ToCells(t2), r.selStart, r.selEnd, "**");
    CHECK_EQ(RunEdit(t2, r2), t);
    CHECK_EQ(r2.selStart, 4);
    CHECK_EQ(r2.selEnd, 9);
}

static void test_edit_wrap_empty_selection_inserts_pair()
{
    using namespace Magpie::Md;
    auto r = WrapSelection(ToCells("ab"), 1, 1, "*");
    CHECK_EQ(RunEdit("ab", r), std::string("a**b"));
    CHECK_EQ(r.cursor, 2);
}

static void test_edit_italic_does_not_strip_bold()
{
    using namespace Magpie::Md;
    // Inner selection of a bold word: italic must wrap, not treat "**" as its markers.
    const std::string t = "**x**";
    auto r = WrapSelection(ToCells(t), 2, 3, "*");
    CHECK_EQ(RunEdit(t, r), std::string("***x***"));
}

static void test_edit_toggle_task_prefix()
{
    using namespace Magpie::Md;
    const std::string t = "one\ntwo";
    auto r = ToggleLinePrefix(ToCells(t), 0, 5, LinePrefix::Task);   // touches both lines
    const std::string t2 = RunEdit(t, r);
    CHECK_EQ(t2, std::string("- [ ] one\n- [ ] two"));
    // All lines are tasks now -> toggling again removes the prefix (checked ones too).
    std::string t3 = t2; t3[3] = 'x';
    auto r2 = ToggleLinePrefix(ToCells(t3), 0, (int)t3.size(), LinePrefix::Task);
    CHECK_EQ(RunEdit(t3, r2), t);
}

static void test_edit_bullet_to_task_keeps_indent()
{
    using namespace Magpie::Md;
    const std::string t = "  - item";
    auto r = ToggleLinePrefix(ToCells(t), 5, 5, LinePrefix::Task);
    CHECK_EQ(RunEdit(t, r), std::string("  - [ ] item"));
    CHECK_EQ(r.cursor, 9);   // caret stays on the same letter
}

static void test_edit_heading_replaces_list_prefix()
{
    using namespace Magpie::Md;
    const std::string t = "  - [x] Title";
    auto r = ToggleLinePrefix(ToCells(t), 0, 0, LinePrefix::Heading2);
    CHECK_EQ(RunEdit(t, r), std::string("## Title"));
    const std::string h1 = "## Title";
    auto r2 = ToggleLinePrefix(ToCells(h1), 4, 4, LinePrefix::Heading1);
    CHECK_EQ(RunEdit(h1, r2), std::string("# Title"));
}

static void test_edit_selection_ending_at_line_start()
{
    using namespace Magpie::Md;
    // Selecting "a\n" (ends at the start of line 2) must only touch line 1.
    const std::string t = "a\nb";
    auto r = ToggleLinePrefix(ToCells(t), 0, 2, LinePrefix::Bullet);
    CHECK_EQ(RunEdit(t, r), std::string("- a\nb"));
}

static void test_edit_enter_continues_task_list()
{
    using namespace Magpie::Md;
    const std::string t = "  - [x] done";
    auto r = ContinueListOnEnter(ToCells(t), (int)t.size());
    CHECK(r.handled);
    CHECK_EQ(RunEdit(t, r), std::string("  - [x] done\n  - [ ] "));
    CHECK_EQ(r.cursor, (int)RunEdit(t, r).size());
}

static void test_edit_enter_on_empty_item_ends_list()
{
    using namespace Magpie::Md;
    const std::string t = "- a\n- ";
    auto r = ContinueListOnEnter(ToCells(t), (int)t.size());
    CHECK(r.handled);
    CHECK_EQ(RunEdit(t, r), std::string("- a\n"));
    CHECK_EQ(r.cursor, 4);
}

static void test_edit_enter_plain_line_not_handled()
{
    using namespace Magpie::Md;
    CHECK(!ContinueListOnEnter(ToCells("hello"), 5).handled);
    CHECK(!ContinueListOnEnter(ToCells("- item"), 1).handled);   // caret inside the marker
}

static void test_edit_chip_cell_is_preserved()
{
    using namespace Magpie::Md;
    // A chip is a single placeholder cell; prefix edits must not touch it.
    Cells c = ToCells("x");
    c[0] = 0xFFFC;
    auto r = ToggleLinePrefix(c, 1, 1, LinePrefix::Task);
    ApplyOps(c, r.ops);
    CHECK(c.size() == 7);
    CHECK(c.back() == 0xFFFCu);
}

int main()
{
    test_create_appears_in_notes();
    test_default_title();
    test_two_creates_have_different_ids();
    test_get_returns_note();
    test_get_returns_nullptr_for_missing();
    test_edit_via_get_persists();
    test_edit_via_create_ref_persists();
    test_delete_removes_note();
    test_delete_noop_for_missing();
    test_json_roundtrip_ordered();
    test_fromjson_invalid_returns_false_and_leaves_state();
    test_export_all_import_roundtrip();
    test_export_selected_ids_only();
    test_group_create_move_delete();
    test_groups_persist_roundtrip();
    test_fromjson_drops_unknown_group_refs();
    test_export_all_includes_groups_and_import_recreates_them();
    test_import_reuses_existing_group_by_id();
    test_export_single_note_and_group();
    test_import_v1_export_without_groups();
    test_group_color_persists_and_travels();
    test_reset_times();
    test_daily_note_resets_after_reset_only();
    test_weekly_and_plain_notes();
    test_reset_skips_note_in_editor();
    test_type_persists_and_travels();
    test_delete_many();
    test_i18n_lookup_and_fallback();
    test_i18n_table_is_consistent();
    test_reimport_same_note_is_skipped();
    test_import_into_source_store_adds_nothing();
    test_prepare_import_flags_duplicates();
    test_changed_duplicate_can_be_updated();
    test_update_ignores_unknown_and_idless_items();
    test_prepare_import_flags_repeats_within_file();
    test_ids_are_unique_format();
    test_fromjson_migrates_legacy_ids();
    test_import_plain_notes_json_array();
    test_import_invalid_leaves_store_untouched();
    test_fromjson_wrong_shape_returns_false();
    test_fromjson_empty_array();
    test_create_after_fromjson_has_unique_id();

    // Decoder Ring ABI header tests
    test_decoder_header_is_v4();

    // ChatLinks codec tests
    test_codec_decode_item_extracts_upgrades();
    test_codec_decode_item_no_upgrades_is_zero();
    test_codec_encode_item_roundtrip();
    test_codec_encode_map_roundtrip();
    test_codec_encode_skill_roundtrip();
    test_codec_encode_skin_roundtrip();
    test_codec_segment_line_plain_and_link();
    test_codec_segment_line_plain_only();
    test_codec_link_type_none_for_garbage();
    test_specdata_elite_spec_name();
    test_specdata_profession_name();

    // ChipResolver tests
    test_chip_item_label();
    test_chip_waypoint_label();
    test_chip_skill_label();
    test_chip_skin_label();
    test_chip_garbage_fallback();
    test_chip_colours_distinct();
    test_chip_non_build_never_returns_build_path();
    test_chip_build_label_via_spec_helpers();
    test_chip_build_roundtrip_via_encode();
    test_chip_build_core_profession_fallback();

    // ChipCodec round-trip tests
    test_chipcodec_roundtrip_plain();
    test_chipcodec_roundtrip_with_chip();
    test_chipcodec_roundtrip_multiline();
    test_chipcodec_roundtrip_mixed();

    // Markdown parser tests
    test_md_heading_level1();
    test_md_heading_level3();
    test_md_bullet();
    test_md_paragraph();
    test_md_bold();
    test_md_italic();
    test_md_chip_inline();
    test_md_bold_with_chip();
    test_md_unmatched_star_is_literal();
    test_md_multiline();
    test_md_heading_with_bold();

    // Task lists
    test_md_task_unchecked();
    test_md_task_checked_both_cases();
    test_md_task_empty_item_and_non_tasks();
    test_md_task_with_bold_and_chip();
    test_md_count_tasks();
    test_md_toggle_task();
    test_md_uncheck_all();

    // Markdown editing commands
    test_edit_wrap_bold_selection();
    test_edit_wrap_empty_selection_inserts_pair();
    test_edit_italic_does_not_strip_bold();
    test_edit_toggle_task_prefix();
    test_edit_bullet_to_task_keeps_indent();
    test_edit_heading_replaces_list_prefix();
    test_edit_selection_ending_at_line_start();
    test_edit_enter_continues_task_list();
    test_edit_enter_on_empty_item_ends_list();
    test_edit_enter_plain_line_not_handled();
    test_edit_chip_cell_is_preserved();

    // IconUrl pure-helper tests
    test_iconurl_key_stable();
    test_iconurl_key_differs();
    test_iconurl_key_null_does_not_crash();
    test_iconurl_split_full_url();
    test_iconurl_split_no_path();
    test_iconurl_split_malformed();
    test_iconurl_split_empty_does_not_crash();
    test_chipcolor_item_rarity_palette();
    test_chipcolor_item_unknown_is_basic();
    test_chipcolor_nonitem_types_are_blue();
    test_chipcolor_combined_rule();

    if (s_failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cerr << s_failures << " test(s) FAILED\n";
    return 1;
}
