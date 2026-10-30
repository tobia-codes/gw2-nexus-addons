#pragma once
//
// ChipCodec -- PURE text <-> cell-model round-trip for the chip text editor.
//
// PURITY: depends ONLY on the C++ standard library and the vendored ChatLinks
// codec. NO ImGui, Nexus, Win32, or game hooks. This is the host-testable slice
// of the ChipTextEdit widget: it owns the parse (chat line -> ordered cells) and
// serialize (cells -> chat line) so the text<->chip mapping can be verified
// without a renderer.
//
// A ParsedCell is one atomic unit of the editor model: either a chip (one whole
// "[&base64]" chat code or one "{{event:<id>}}" token, see Events.h) or a single
// text codepoint (which may be '\n'). The
// widget's ChipCell mirrors this model, adding only per-frame render fields.
//
#include <string>
#include <vector>
#include <cstdint>
#include "chat/ChatLinks.h"

namespace Trailpack {

// One atomic cell of the editor model.
struct ParsedCell {
    bool        isChip = false;  // true: a whole chat code; false: a single text codepoint
    uint32_t    cp = 0;          // Unicode codepoint (text cell; may be '\n')
    std::string code;            // chip cell: "[&base64]" chat code or "{{event:<id>}}"
};

// Parse a UTF-8 string into ordered cells: each chat link / AE2 code and each event
// token becomes ONE chip cell ({isChip=true, code}); all other text (incl. '\n')
// becomes one text cell per codepoint ({isChip=false, cp}).
std::vector<ParsedCell> ParseToCells(const std::string& utf8);

// Reassemble cells back into a UTF-8 string: a chip emits its code verbatim; a
// text cell emits its codepoint's UTF-8 (newlines preserved). Inverse of
// ParseToCells for any string it produced.
std::string CellsToText(const std::vector<ParsedCell>& cells);

// --- Soft wrap (the editor's visual rows) ---
// One cell as the wrap sees it: its width, and whether it is a '\n' or a space.
struct WrapCell {
    float width   = 0.0f;
    bool  newline = false;
    bool  space   = false;
};

// Number of cells in the visual row that starts at `start` (of `count`), for rows at most
// `maxWidth` wide: up to and including a newline; else as many cells as fit, broken after the
// last space (spaces may hang past the edge, so no row starts with one), or -- for a word or a
// chip wider than the row -- before the cell that overflows. At least 1 while start < count.
// `get(i)` returns the WrapCell of cell i.
template <class GetCell>
int WrapRowLength(int count, int start, float maxWidth, GetCell get)
{
    float w = 0.0f;
    int lastSpace = -1;
    for (int i = start; i < count; ++i) {
        const WrapCell c = get(i);
        if (c.newline) return i - start + 1;
        if (c.space) { lastSpace = i; w += c.width; continue; }
        if (w + c.width > maxWidth && i > start)
            return lastSpace >= start ? lastSpace - start + 1 : i - start;
        w += c.width;
    }
    return count - start;
}

// --- UTF-8 helpers (shared with the widget) ---
void                      AppendUtf8(std::string& s, uint32_t cp);
std::vector<uint32_t>     DecodeUtf8(const std::string& s);

} // namespace Trailpack
