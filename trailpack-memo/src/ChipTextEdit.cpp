#include "ChipTextEdit.h"
#include "ChipResolver.h"      // Magpie::ResolveChip (replaces Pie UI's RichLineResolveChip)
#include "EventChip.h"         // event tokens: name-only label + colour (D10)
#include "Events.h"
#include "ChipRich.h"          // Magpie::ChipLabel (resolved name when Decoder Ring is warm)
#include "ChipCodec.h"         // pure text<->cell model + UTF-8 helpers
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

// stb_textedit, included in two passes (the standard pattern ImGui itself uses): first for the
// TYPES (StbTexteditRow / STB_TexteditState -- default CHARTYPE int), then again with our macros +
// IMPLEMENTATION (the include guard skips the types the 2nd time). We deliberately do NOT include
// imgui_internal.h, so this is the ONLY stb_textedit instance in the TU -- no guard/symbol clash
// with ImGui's own ImStb-namespaced copy.
#include "imstb_textedit.h"    // pass 1: types only

static inline int   ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- stb_textedit glue ------------------------------------------------------------------------
// Our STRING type is ChipTextEdit; each "character" is a ChipCell (text codepoint OR an atomic
// chip). Widths are per-cell so a chip occupies its rendered token width. A newline cell
// (isChip==false, cp=='\n') has the sentinel width so stb treats it as a line break.
namespace Magpie {

static inline bool IsNewlineCell(const ChipCell& c) { return !c.isChip && c.cp == '\n'; }

static int   ChipTE_StringLen(ChipTextEdit* o)            { return (int)o->cells.size(); }
static int   ChipTE_GetChar(ChipTextEdit* o, int i)       { return o->cells[i].isChip ? 0xFFFC : (int)o->cells[i].cp; }
static float ChipTE_GetWidth(ChipTextEdit* o, int ls, int i) {
    // stb's GETWIDTH(obj, n, i) contract: `i` is the offset of the character RELATIVE to the
    // row that starts at character `n` (=ls), so the absolute cell index is `ls + i`. (The
    // single-line original ignored `ls` because its only row starts at 0; multi-line rows do not.)
    // -1.0f == STB_TEXTEDIT_GETWIDTH_NEWLINE (defined below); return the literal so we don't depend
    // on macro visibility order. stb compares the returned width against the sentinel.
    const ChipCell& c = o->cells[ls + i];
    return IsNewlineCell(c) ? -1.0f : c.width;
}

// Cells of the visual row starting at `n`: soft-wrapped at o->wrapWidth (ChipCodec's
// WrapRowLength), up to and including a newline cell. Every row-aware place uses this, so
// clicks, Up/Down, Home/End, caret, selection and drawing all agree.
static int RowLen(const ChipTextEdit* o, int n) {
    return WrapRowLength((int)o->cells.size(), n, o->wrapWidth, [o](int i) {
        const ChipCell& c = o->cells[i];
        WrapCell w;
        w.width   = c.width;
        w.newline = IsNewlineCell(c);
        w.space   = !c.isChip && c.cp == ' ';
        return w;
    });
}

// Lay out ONE visual row beginning at cell `n`: its cells (RowLen) and the summed width of its
// NON-newline cells. This is what lets stb map (x,y) clicks to the right row and Up/Down move
// between rows.
static void  ChipTE_Layout(StbTexteditRow* r, ChipTextEdit* o, int n) {
    const float lh = ImGui::GetTextLineHeight();
    const int count = RowLen(o, n);
    float w = 0.0f;
    for (int i = n; i < n + count; ++i)
        if (!IsNewlineCell(o->cells[i])) w += o->cells[i].width;
    r->x0 = 0.0f;
    r->x1 = w;
    r->baseline_y_delta = lh;
    r->ymin = 0.0f;
    r->ymax = lh;
    r->num_chars = count;
}

static void  ChipTE_DeleteChars(ChipTextEdit* o, int i, int n) {
    if (i < 0 || n <= 0 || i >= (int)o->cells.size()) return;
    n = std::min(n, (int)o->cells.size() - i);
    o->cells.erase(o->cells.begin() + i, o->cells.begin() + i + n);
}
static int   ChipTE_InsertChars(ChipTextEdit* o, int i, const int* chars, int n) {
    if (i < 0 || i > (int)o->cells.size()) return 0;
    std::vector<ChipCell> ins; ins.reserve(n);
    // Arbitrary codepoints, including '\n' (a newline typed via stb_textedit_key('\n')), become
    // ordinary text cells -- IsNewlineCell() then recognises them.
    for (int k = 0; k < n; ++k) { ChipCell c; c.isChip = false; c.cp = (unsigned)chars[k]; ins.push_back(std::move(c)); }
    o->cells.insert(o->cells.begin() + i, ins.begin(), ins.end());
    return 1;
}

} // namespace Magpie

// stb_textedit configuration (mirrors ImGui's, so editing behaviour matches its InputText).
#define STB_TEXTEDIT_CHARTYPE       int
#define STB_TEXTEDIT_POSITIONTYPE   int
#define STB_TEXTEDIT_STRING         Magpie::ChipTextEdit
#define STB_TEXTEDIT_STRINGLEN(o)            Magpie::ChipTE_StringLen(o)
#define STB_TEXTEDIT_GETCHAR(o,i)            Magpie::ChipTE_GetChar(o,i)
#define STB_TEXTEDIT_GETWIDTH(o,ls,i)        Magpie::ChipTE_GetWidth(o,ls,i)
#define STB_TEXTEDIT_LAYOUTROW(r,o,ls)       Magpie::ChipTE_Layout(r,o,ls)
#define STB_TEXTEDIT_DELETECHARS(o,i,n)      Magpie::ChipTE_DeleteChars(o,i,n)
#define STB_TEXTEDIT_INSERTCHARS(o,i,c,n)    Magpie::ChipTE_InsertChars(o,i,c,n)
#define STB_TEXTEDIT_KEYTOTEXT(k)            ((k) >= 0x200000 ? 0 : (k))
#define STB_TEXTEDIT_NEWLINE                 '\n'
#define STB_TEXTEDIT_GETWIDTH_NEWLINE        (-1.0f)
#define STB_TEXTEDIT_K_LEFT       0x200000
#define STB_TEXTEDIT_K_RIGHT      0x200001
#define STB_TEXTEDIT_K_UP         0x200002
#define STB_TEXTEDIT_K_DOWN       0x200003
#define STB_TEXTEDIT_K_LINESTART  0x200004
#define STB_TEXTEDIT_K_LINEEND    0x200005
#define STB_TEXTEDIT_K_TEXTSTART  0x200006
#define STB_TEXTEDIT_K_TEXTEND    0x200007
#define STB_TEXTEDIT_K_DELETE     0x200008
#define STB_TEXTEDIT_K_BACKSPACE  0x200009
#define STB_TEXTEDIT_K_UNDO       0x20000A
#define STB_TEXTEDIT_K_REDO       0x20000B
#define STB_TEXTEDIT_K_WORDLEFT   0x20000C
#define STB_TEXTEDIT_K_WORDRIGHT  0x20000D
#define STB_TEXTEDIT_K_PGUP       0x20000E
#define STB_TEXTEDIT_K_PGDOWN     0x20000F
#define STB_TEXTEDIT_K_SHIFT      0x400000
#define STB_TEXTEDIT_IMPLEMENTATION
#include "imstb_textedit.h"

namespace Magpie {

// Focus timestamp (ms, steady clock) for the WndProc keyboard-capture reinforcement (see header).
static std::atomic<long long> s_lastFocusMs{-100000};
static long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static void StampChipFocus() { s_lastFocusMs.store(NowMs(), std::memory_order_relaxed); }
bool ChipInputActive() {
    constexpr long long kHold = 60;   // ms; > a slow frame, < a perceptible input lag
    return (NowMs() - s_lastFocusMs.load(std::memory_order_relaxed)) < kHold;
}

// Monotonic blur request: the WndProc bumps it on a game-world click; each chip input releases
// focus once per bump (compared against its own blurSeen) on its next Render.
static std::atomic<unsigned int> s_blurSeq{0};
void RequestChipBlur() { s_blurSeq.fetch_add(1, std::memory_order_relaxed); }

// Lazily allocate + initialise the (opaque) stb state. is_single_line = 0 -> MULTI-LINE.
static STB_TexteditState& St(ChipTextEdit* o) {
    if (!o->m_stb) { auto* s = new STB_TexteditState(); stb_textedit_initialize_state(s, 0); o->m_stb = s; }
    return *static_cast<STB_TexteditState*>(o->m_stb);
}

ChipTextEdit::~ChipTextEdit() { delete static_cast<STB_TexteditState*>(m_stb); m_stb = nullptr; }

// ---- per-frame cell measurement ---------------------------------------------------------------
static void Measure(std::vector<ChipCell>& cells) {
    ImFont* font = ImGui::GetFont();
    const float scale = ImGui::GetFontSize() / font->FontSize;
    for (auto& c : cells) {
        if (c.isChip) {
            // Event token: its name only (no countdown, so the width stays put). Chat code:
            // resolved name from Decoder Ring when warm, structural label otherwise
            // (ChipLabel never returns empty); colour matches the view-mode chips:
            // native GW2 type colour, items by rarity (Basic until resolved).
            if (Events::IsToken(c.code)) {
                c.name = Magpie::EventChipLabel(c.code); c.color = Magpie::EventChipColor(c.code);
            } else {
                c.name = Magpie::ChipLabel(c.code); c.color = Magpie::ChipDisplayColor(c.code);
            }
            c.width = ImGui::CalcTextSize(c.name.c_str()).x + 6.0f;   // small side padding
        } else if (IsNewlineCell(c)) {
            c.width = 0.0f;   // visual width; the GETWIDTH sentinel is handled in ChipTE_GetWidth
        } else {
            c.width = font->GetCharAdvance((ImWchar)c.cp) * scale;
        }
    }
}

void ChipTextEdit::Clear() { cells.clear(); stb_textedit_initialize_state(&St(this), 0); }

void ChipTextEdit::InsertChip(const std::string& code) {
    STB_TexteditState& s = St(this);
    int at = ClampI(s.cursor, 0, (int)cells.size());
    ChipCell c; c.isChip = true; c.code = code;
    if (Events::IsToken(code)) {
        c.name = Magpie::EventChipLabel(code); c.color = Magpie::EventChipColor(code);
    } else {
        ChipView v = Magpie::ResolveChip(code);
        c.name = v.label.empty() ? "[link]" : v.label;
        c.color = v.color;
    }
    cells.insert(cells.begin() + at, std::move(c));
    s.cursor = at + 1;
    s.select_start = s.select_end = s.cursor;
}

std::string ChipTextEdit::GetText() const {
    std::string out;
    for (const auto& c : cells) { if (c.isChip) out += c.code; else AppendUtf8(out, c.cp); }
    return out;
}

void ChipTextEdit::SetText(const std::string& utf8) {
    cells.clear();
    // Delegate the text->cell parse to the pure ChipCodec, then attach the widget's render fields.
    for (const auto& pc : ParseToCells(utf8)) {
        ChipCell c; c.isChip = pc.isChip; c.cp = pc.cp; c.code = pc.code;
        if (c.isChip && Events::IsToken(c.code)) {
            c.name = Magpie::EventChipLabel(c.code); c.color = Magpie::EventChipColor(c.code);
        } else if (c.isChip) {
            ChipView v = Magpie::ResolveChip(c.code);
            c.name = v.label.empty() ? "[link]" : v.label;
            c.color = v.color;
        }
        cells.push_back(std::move(c));
    }
    STB_TexteditState& s = St(this);
    s.cursor = (int)cells.size();
    s.select_start = s.select_end = s.cursor;
}

Md::Cells ChipTextEdit::Codepoints() const {
    Md::Cells out; out.reserve(cells.size());
    for (const auto& c : cells) out.push_back(c.isChip ? 0xFFFCu : c.cp);
    return out;
}

void ChipTextEdit::GetSelection(int& selStart, int& selEnd) const {
    const int n = (int)cells.size();
    if (!m_stb) { selStart = selEnd = n; return; }
    const auto& s = *static_cast<const STB_TexteditState*>(m_stb);
    if (s.select_start != s.select_end) {
        selStart = ClampI(std::min(s.select_start, s.select_end), 0, n);
        selEnd   = ClampI(std::max(s.select_start, s.select_end), 0, n);
    } else {
        selStart = selEnd = ClampI(s.cursor, 0, n);
    }
}

void ChipTextEdit::ApplyEdit(const Md::EditResult& r) {
    if (!r.handled) return;
    STB_TexteditState& s = St(this);
    // Back-to-front so earlier positions stay valid. Ops only touch plain text (never chips), so
    // stb's own undo records can capture them (it reads GETCHAR before the delete).
    for (auto it = r.ops.rbegin(); it != r.ops.rend(); ++it) {
        const int pos = ClampI(it->pos, 0, (int)cells.size());
        const int del = ClampI(it->del, 0, (int)cells.size() - pos);
        stb_text_makeundo_replace(this, &s, pos, del, (int)it->ins.size());
        ChipTE_DeleteChars(this, pos, del);
        if (!it->ins.empty()) {
            std::vector<int> chars(it->ins.begin(), it->ins.end());
            ChipTE_InsertChars(this, pos, chars.data(), (int)chars.size());
        }
    }
    const int n = (int)cells.size();
    s.select_start   = ClampI(r.selStart, 0, n);
    s.select_end     = ClampI(r.selEnd, 0, n);
    s.cursor         = ClampI(r.cursor, 0, n);
    s.has_preferred_x = 0;
    focusNext = true;
}

// ---- row / position geometry ------------------------------------------------------------------
// First cell of every visual row (soft wrap included); a trailing newline opens an empty last
// row. Never empty: an empty text is one row starting at 0.
static std::vector<int> RowStarts(const ChipTextEdit& o) {
    std::vector<int> starts{ 0 };
    const int n = (int)o.cells.size();
    for (int i = 0; i < n;) {
        i += RowLen(&o, i);
        if (i < n || (i == n && IsNewlineCell(o.cells[n - 1]))) starts.push_back(i);
    }
    return starts;
}

// Row of cell position `i` (a position on a row boundary belongs to the row it starts).
static int RowOf(const std::vector<int>& starts, int i) {
    return (int)(std::upper_bound(starts.begin(), starts.end(), i) - starts.begin()) - 1;
}

// x of position `i` within the row starting at `rowStart` (newline cells have no width).
static float XInRow(const std::vector<ChipCell>& cells, int rowStart, int i) {
    float x = 0.0f;
    for (int k = rowStart; k < i; ++k) if (!IsNewlineCell(cells[k])) x += cells[k].width;
    return x;
}

// Map caret index `i` (0..size) to (visual row, x-offset-within-row).
static void CaretPos(const std::vector<ChipCell>& cells, const std::vector<int>& starts, int i,
                     int& row, float& x) {
    i = ClampI(i, 0, (int)cells.size());
    row = RowOf(starts, i);
    x = XInRow(cells, starts[row], i);
}

bool ChipTextEdit::Render(const char* id, float width, ImU32 borderCol, int minLines) {
    STB_TexteditState& s = St(this);
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    Measure(cells);
    const float lh = ImGui::GetTextLineHeight();

    // Inner padding like the list / note panes (Theme::PushPaneBorder): Nexus's FramePadding is
    // only a few px, which glues the text to the border in a multi-line field.
    const float fs = ImGui::GetFontSize();
    const ImVec2 pad(std::max(style.FramePadding.x, fs * 0.5f), std::max(style.FramePadding.y, fs * 0.4f));

    // Soft wrap at the text area's width (before any stb call, which lays out rows).
    wrapWidth = std::max(1.0f, width - pad.x * 2.0f);

    const int rows = std::max((int)RowStarts(*this).size(), std::max(1, minLines));
    const float contentH = rows * lh;
    const float h = contentH + pad.y * 2.0f;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    // No ImGuiMouseCursor_TextInput: GW2 / Nexus shows it as a gear (see AddonRender).

    // Focus: claim on click inside; release on a click that isn't ours.
    if (ImGui::IsItemClicked()) focused = true;
    else if (io.MouseClicked[0] && !hovered) focused = false;
    {
        unsigned int seq = s_blurSeq.load(std::memory_order_relaxed);
        if (seq != blurSeen) { blurSeen = seq; if (!ImGui::IsItemClicked()) focused = false; }
    }
    if (focusNext) { focused = true; focusNext = false; }
    if (focused) {
        ImGui::CaptureKeyboardFromApp(true);
        io.WantCaptureKeyboard = true;
        io.WantTextInput       = true;
        StampChipFocus();
    }

    const ImVec2 inner_min(p0.x + pad.x, p0.y + pad.y);
    const float  inner_w = width - pad.x * 2.0f;

    bool changed = false;
    const int sizeBefore = (int)cells.size();

    // ---- input (only while focused) ----
    if (focused) {
        // Mouse: map screen (x,y) to text-local coords, real y so clicks land on the right row.
        const float mx = io.MousePos.x - inner_min.x;
        const float my = io.MousePos.y - inner_min.y;
        if (ImGui::IsItemClicked()) stb_textedit_click(this, &s, mx, my);
        else if (held && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) stb_textedit_drag(this, &s, mx, my);

        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c != 0x7F) { stb_textedit_key(this, &s, c); changed = true; }
        }
        auto K = [&](ImGuiKey k){ int idx = io.KeyMap[k]; return idx >= 0 && ImGui::IsKeyPressed(idx); };
        const int sh = io.KeyShift ? STB_TEXTEDIT_K_SHIFT : 0;
        const bool word = io.KeyCtrl;
        if (K(ImGuiKey_LeftArrow))   stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDLEFT  : STB_TEXTEDIT_K_LEFT)  | sh);
        if (K(ImGuiKey_RightArrow))  stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | sh);
        if (K(ImGuiKey_UpArrow))     stb_textedit_key(this, &s, STB_TEXTEDIT_K_UP   | sh);
        if (K(ImGuiKey_DownArrow))   stb_textedit_key(this, &s, STB_TEXTEDIT_K_DOWN | sh);
        if (K(ImGuiKey_PageUp))      stb_textedit_key(this, &s, STB_TEXTEDIT_K_PGUP | sh);
        if (K(ImGuiKey_PageDown))    stb_textedit_key(this, &s, STB_TEXTEDIT_K_PGDOWN | sh);
        if (K(ImGuiKey_Home))        stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINESTART | sh);
        if (K(ImGuiKey_End))         stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINEEND | sh);
        if (K(ImGuiKey_Delete))    { stb_textedit_key(this, &s, STB_TEXTEDIT_K_DELETE | sh); changed = true; }
        if (K(ImGuiKey_Backspace)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_BACKSPACE | sh); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_Z)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_UNDO); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_Y)) { stb_textedit_key(this, &s, STB_TEXTEDIT_K_REDO); changed = true; }
        if (io.KeyCtrl && K(ImGuiKey_A)) { s.select_start = 0; s.select_end = (int)cells.size(); s.cursor = (int)cells.size(); }
        if (io.KeyCtrl && (K(ImGuiKey_C) || K(ImGuiKey_X))) {
            int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
            if (b > a) {
                std::string sub; for (int k = a; k < b; ++k) { if (cells[k].isChip) sub += cells[k].code; else AppendUtf8(sub, cells[k].cp); }
                ImGui::SetClipboardText(sub.c_str());
                if (K(ImGuiKey_X)) { stb_textedit_cut(this, &s); changed = true; }
            }
        }
        if (io.KeyCtrl && K(ImGuiKey_V)) {
            const char* clip = ImGui::GetClipboardText();
            if (clip && *clip) {
                // Same parse as SetText: chat codes and event tokens become chips.
                for (const auto& pc : ParseToCells(clip)) {
                    if (pc.isChip) InsertChip(pc.code);
                    else if (pc.cp >= 0x20 || pc.cp == '\n') stb_textedit_key(this, &s, (int)pc.cp);
                }
                changed = true;
            }
        }
        // MULTI-LINE: Enter INSERTS a newline (it does NOT submit). On a list line it continues
        // the list instead (or ends it on an empty item).
        if (K(ImGuiKey_Enter) || K(ImGuiKey_KeyPadEnter)) {
            Md::EditResult lr;
            if (continueLists && s.select_start == s.select_end)
                lr = Md::ContinueListOnEnter(Codepoints(), s.cursor);
            if (lr.handled) ApplyEdit(lr);
            else            stb_textedit_key(this, &s, '\n');
            changed = true;
        }
        // Emphasis shortcuts. ImGuiKey has no letter entries for B/I in this ImGui version, so
        // test the raw virtual-key codes (Win32 VK == ASCII uppercase).
        if (io.KeyCtrl && !io.KeyShift && (ImGui::IsKeyPressed('B') || ImGui::IsKeyPressed('I'))) {
            int a, b; GetSelection(a, b);
            ApplyEdit(Md::WrapSelection(Codepoints(), a, b, ImGui::IsKeyPressed('B') ? "**" : "*"));
            changed = true;
        }
        if (K(ImGuiKey_Escape)) focused = false;

        Measure(cells);   // input may have changed cells; re-measure for the draw below
    }
    if ((int)cells.size() != sizeBefore) changed = true;

    // ---- draw ----
    const std::vector<int> rowStarts = RowStarts(*this);   // after this frame's edits
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + h);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (borderCol & 0xFF000000) dl->AddRect(p0, p1, borderCol, style.FrameRounding);

    dl->PushClipRect(ImVec2(inner_min.x, p0.y), ImVec2(p1.x - pad.x, p1.y), true);

    // Selection: one rect per row the [a,b) range covers.
    if (focused && s.select_start != s.select_end) {
        int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
        // One rect per visual row the [a,b) range covers.
        const ImU32 selCol = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
        for (int r = RowOf(rowStarts, a); r < (int)rowStarts.size() && rowStarts[r] < b; ++r) {
            const int rs = rowStarts[r];
            const int re = r + 1 < (int)rowStarts.size() ? rowStarts[r + 1] : (int)cells.size();
            const int lo = std::max(a, rs), hi = std::min(b, re);
            float x0 = XInRow(cells, rs, lo), x1 = XInRow(cells, rs, hi);
            if (x1 <= x0) x1 = x0 + 2.0f;   // a selected newline shows a thin marker
            const float yy = inner_min.y + r * lh;
            dl->AddRectFilled(ImVec2(inner_min.x + x0, yy), ImVec2(inner_min.x + x1, yy + lh), selCol);
        }
    }

    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    float x = inner_min.x;
    float y = inner_min.y;
    std::string run; float runStart = x;
    auto flushRun = [&]() {
        if (!run.empty()) { dl->AddText(ImVec2(runStart, y), textCol, run.c_str()); run.clear(); }
    };
    int row = 0;
    for (int k = 0; k < (int)cells.size(); ++k) {
        // A new visual row (soft wrap, or after a newline) starts here.
        if (row + 1 < (int)rowStarts.size() && k == rowStarts[row + 1]) {
            flushRun();
            ++row;
            x = inner_min.x; runStart = x;
            y = inner_min.y + row * lh;
        }
        const ChipCell& c = cells[k];
        if (c.isChip) {
            flushRun();
            ImU32 bg = (c.color & 0x00FFFFFF) | 0x33000000;   // faint tint of the chip colour
            dl->AddRectFilled(ImVec2(x + 1, y), ImVec2(x + c.width - 1, y + lh), bg, 3.0f);
            dl->AddText(ImVec2(x + 3, y), c.color, c.name.c_str());
            x += c.width; runStart = x;
        } else if (IsNewlineCell(c)) {
            flushRun();   // the next row starts at the following cell (rowStarts)
        } else {
            AppendUtf8(run, c.cp);
            x += c.width;
        }
    }
    flushRun();

    // Caret at the cursor's (row, x).
    if (focused) {
        int crow; float cx; CaretPos(cells, rowStarts, s.cursor, crow, cx);
        float caretX = inner_min.x + cx;
        float caretY = inner_min.y + crow * lh;
        if (std::fmod((float)ImGui::GetTime(), 1.0f) < 0.6f)
            dl->AddLine(ImVec2(caretX, caretY), ImVec2(caretX, caretY + lh), textCol);
    }

    dl->PopClipRect();
    return changed;
}

} // namespace Magpie
