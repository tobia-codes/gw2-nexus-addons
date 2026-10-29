#include "MarkdownEdit.h"
#include "ChipCodec.h"   // AppendUtf8 / DecodeUtf8
#include <algorithm>

namespace Trailpack { namespace Md {

namespace {

// ---------------------------------------------------------------------------
// Line geometry helpers (cell indices; '\n' cells separate lines).
// ---------------------------------------------------------------------------
int LineStart(const Cells& t, int pos)
{
    pos = std::clamp(pos, 0, (int)t.size());
    while (pos > 0 && t[pos - 1] != '\n') --pos;
    return pos;
}

int LineEnd(const Cells& t, int pos)
{
    pos = std::clamp(pos, 0, (int)t.size());
    while (pos < (int)t.size() && t[pos] != '\n') ++pos;
    return pos;
}

bool MatchAt(const Cells& t, int pos, const char* s)
{
    for (int k = 0; s[k]; ++k) {
        if (pos + k >= (int)t.size() || t[pos + k] != (unsigned char)s[k]) return false;
    }
    return true;
}

Cells Str(const char* s)
{
    Cells out;
    for (int k = 0; s[k]; ++k) out.push_back((unsigned char)s[k]);
    return out;
}

// The block prefix found at the start of one line.
struct PrefixInfo {
    int  indent = 0;          // leading spaces
    int  len    = 0;          // prefix length after the indent (0 = none)
    bool isHeading = false;
    int  headingLevel = 0;
    bool isBullet  = false;
    bool isTask    = false;
};

PrefixInfo ReadPrefix(const Cells& t, int lineStart, int lineEnd)
{
    PrefixInfo p;
    int i = lineStart;
    // Headings only count at column 0 (matches Markdown.cpp's parser).
    if (i < lineEnd && t[i] == '#') {
        int lvl = 0;
        while (i + lvl < lineEnd && t[i + lvl] == '#') ++lvl;
        if (i + lvl < lineEnd && t[i + lvl] == ' ') {
            p.isHeading = true; p.headingLevel = lvl; p.len = lvl + 1;
            return p;
        }
    }
    while (i < lineEnd && t[i] == ' ') ++i;
    p.indent = i - lineStart;
    if (MatchAt(t, i, "- [") && i + 4 < lineEnd) {
        const unsigned int st = t[i + 3];
        if ((st == ' ' || st == 'x' || st == 'X') && t[i + 4] == ']') {
            const int after = i + 5;
            if (after == lineEnd)  { p.isTask = true; p.len = 5; return p; }
            if (t[after] == ' ')   { p.isTask = true; p.len = 6; return p; }
        }
    }
    if (i + 1 < lineEnd && t[i] == '-' && t[i + 1] == ' ') {
        p.isBullet = true; p.len = 2;
    }
    return p;
}

bool HasKind(const PrefixInfo& p, LinePrefix kind)
{
    switch (kind) {
        case LinePrefix::Heading1: return p.isHeading && p.headingLevel == 1;
        case LinePrefix::Heading2: return p.isHeading && p.headingLevel == 2;
        case LinePrefix::Bullet:   return p.isBullet;
        case LinePrefix::Task:     return p.isTask;
    }
    return false;
}

const char* PrefixText(LinePrefix kind)
{
    switch (kind) {
        case LinePrefix::Heading1: return "# ";
        case LinePrefix::Heading2: return "## ";
        case LinePrefix::Bullet:   return "- ";
        case LinePrefix::Task:     return "- [ ] ";
    }
    return "";
}

// Map a position in the original text through ops (ascending) to the edited text. A position
// strictly inside a replaced range lands at the end of the inserted text.
int MapPos(int pos, const std::vector<EditOp>& ops)
{
    int delta = 0;
    for (const EditOp& op : ops) {
        if (pos < op.pos) break;
        if (op.del == 0 && pos == op.pos) {            // pure insertion at pos pushes it right
            delta += (int)op.ins.size();
            continue;
        }
        if (pos >= op.pos + op.del) {                   // op lies entirely before pos
            delta += (int)op.ins.size() - op.del;
            continue;
        }
        return op.pos + delta + (int)op.ins.size();    // inside a replaced range
    }
    return pos + delta;
}

EditResult Finish(std::vector<EditOp> ops, int selA, int selB)
{
    EditResult r;
    r.handled = true;
    std::sort(ops.begin(), ops.end(), [](const EditOp& a, const EditOp& b) { return a.pos < b.pos; });
    r.selStart = MapPos(selA, ops);
    r.selEnd   = MapPos(selB, ops);
    r.cursor   = r.selEnd;
    r.ops      = std::move(ops);
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
EditResult WrapSelection(const Cells& t, int selA, int selB, const std::string& marker)
{
    const int n = (int)t.size();
    int a = std::clamp(std::min(selA, selB), 0, n);
    int b = std::clamp(std::max(selA, selB), 0, n);
    const int m = (int)marker.size();
    const Cells mk = Str(marker.c_str());

    // A single '*' must not be half of a "**" pair (keeps italic from eating bold markers).
    auto isMarkerAt = [&](int pos) {
        if (pos < 0 || pos + m > n || !MatchAt(t, pos, marker.c_str())) return false;
        if (m == 1 && marker[0] == '*') {
            if (pos > 0 && t[pos - 1] == '*') return false;
            if (pos + 1 < n && t[pos + 1] == '*') return false;
        }
        return true;
    };

    EditResult r;
    r.handled = true;

    // Toggle off: markers directly outside the selection, or the selection includes them.
    if (b - a >= 2 * m && isMarkerAt(a) && isMarkerAt(b - m)) {
        r.ops = { EditOp{a, m, {}}, EditOp{b - m, m, {}} };
        r.selStart = a; r.selEnd = b - 2 * m; r.cursor = r.selEnd;
        return r;
    }
    if (a - m >= 0 && b + m <= n && isMarkerAt(a - m) && isMarkerAt(b) && b > a) {
        r.ops = { EditOp{a - m, m, {}}, EditOp{b, m, {}} };
        r.selStart = a - m; r.selEnd = b - m; r.cursor = r.selEnd;
        return r;
    }

    if (a == b) {
        Cells pair = mk; pair.insert(pair.end(), mk.begin(), mk.end());
        r.ops = { EditOp{a, 0, pair} };
        r.selStart = r.selEnd = r.cursor = a + m;
        return r;
    }

    r.ops = { EditOp{a, 0, mk}, EditOp{b, 0, mk} };
    r.selStart = a + m; r.selEnd = b + m; r.cursor = r.selEnd;
    return r;
}

// ---------------------------------------------------------------------------
EditResult ToggleLinePrefix(const Cells& t, int selA, int selB, LinePrefix kind)
{
    const int n = (int)t.size();
    int a = std::clamp(std::min(selA, selB), 0, n);
    int b = std::clamp(std::max(selA, selB), 0, n);
    // A selection ending right at a line start (after a '\n') doesn't touch that line.
    if (b > a && t[b - 1] == '\n') --b;

    struct L { int start, end; PrefixInfo p; };
    std::vector<L> lines;
    for (int ls = LineStart(t, a);;) {
        const int le = LineEnd(t, ls);
        lines.push_back({ ls, le, ReadPrefix(t, ls, le) });
        if (le >= b || le >= n) break;
        ls = le + 1;
    }

    const bool allHave = std::all_of(lines.begin(), lines.end(),
                                     [&](const L& l) { return HasKind(l.p, kind); });
    const bool heading = (kind == LinePrefix::Heading1 || kind == LinePrefix::Heading2);

    std::vector<EditOp> ops;
    for (const L& l : lines) {
        const int prefixPos = l.start + l.p.indent;
        if (allHave) {
            ops.push_back({ prefixPos, l.p.len, {} });
        } else if (heading) {
            // Headings live at column 0: drop the indent along with any old prefix.
            ops.push_back({ l.start, l.p.indent + l.p.len, Str(PrefixText(kind)) });
        } else {
            ops.push_back({ prefixPos, l.p.len, Str(PrefixText(kind)) });
        }
    }
    return Finish(std::move(ops), selA, selB);
}

// ---------------------------------------------------------------------------
EditResult ContinueListOnEnter(const Cells& t, int cursor)
{
    const int n = (int)t.size();
    cursor = std::clamp(cursor, 0, n);
    const int ls = LineStart(t, cursor);
    const int le = LineEnd(t, cursor);
    const PrefixInfo p = ReadPrefix(t, ls, le);
    EditResult r;
    if (!(p.isBullet || p.isTask)) return r;

    const int contentStart = ls + p.indent + p.len;
    if (cursor < contentStart) return r;   // caret inside the marker: plain newline

    bool emptyItem = true;
    for (int k = contentStart; k < le; ++k) if (t[k] != ' ') { emptyItem = false; break; }

    r.handled = true;
    if (emptyItem) {
        // Second Enter on an empty item ends the list: strip the whole line back to empty.
        r.ops = { EditOp{ ls, le - ls, {} } };
        r.selStart = r.selEnd = r.cursor = ls;
        return r;
    }

    Cells ins = { '\n' };
    ins.insert(ins.end(), p.indent, ' ');
    const Cells pre = Str(p.isTask ? "- [ ] " : "- ");
    ins.insert(ins.end(), pre.begin(), pre.end());
    r.ops = { EditOp{ cursor, 0, ins } };
    r.selStart = r.selEnd = r.cursor = cursor + (int)ins.size();
    return r;
}

// ---------------------------------------------------------------------------
void ApplyOps(Cells& text, const std::vector<EditOp>& ops)
{
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const int pos = std::clamp(it->pos, 0, (int)text.size());
        const int del = std::clamp(it->del, 0, (int)text.size() - pos);
        text.erase(text.begin() + pos, text.begin() + pos + del);
        text.insert(text.begin() + pos, it->ins.begin(), it->ins.end());
    }
}

Cells ToCells(const std::string& utf8)
{
    Cells out;
    for (unsigned int cp : DecodeUtf8(utf8)) out.push_back(cp);
    return out;
}

std::string FromCells(const Cells& cells)
{
    std::string out;
    for (unsigned int cp : cells) AppendUtf8(out, cp);
    return out;
}

}} // namespace Trailpack::Md
