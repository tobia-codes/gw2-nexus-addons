#pragma once
// MarkdownEdit.h — pure (ImGui-free) markdown editing commands for the note editor.
//
// The editor's text is modelled as a sequence of "cells": one per codepoint, with a chat-link chip
// occupying exactly ONE cell (pass any placeholder codepoint for it, e.g. 0xFFFC). Commands never
// insert or delete chips — they only add/remove plain markdown markers — so the result is a list of
// small text operations the editor applies to its cell vector, plus the new selection.

#include <string>
#include <vector>

namespace Magpie { namespace Md {

using Cells = std::vector<unsigned int>;

struct EditOp {
    int   pos = 0;    // cell index in the ORIGINAL text
    int   del = 0;    // cells to delete at pos (always plain text)
    Cells ins;        // codepoints to insert at pos
};

struct EditResult {
    bool                handled = false;   // false -> caller falls back to its default behaviour
    std::vector<EditOp> ops;               // ascending by pos, non-overlapping; apply back-to-front
    int                 selStart = 0;      // new selection/caret, in the EDITED text
    int                 selEnd   = 0;
    int                 cursor   = 0;
};

enum class LinePrefix { Heading1, Heading2, Bullet, Task };

// Wrap [selA, selB) in `marker` ("**" bold, "*" italic). If the selection is already wrapped in
// exactly that marker, the markers are removed instead. Empty selection inserts a marker pair with
// the caret between them.
EditResult WrapSelection(const Cells& text, int selA, int selB, const std::string& marker);

// Toggle a block prefix on every line touched by [selA, selB). If all touched lines already carry
// `kind`, it is removed; otherwise each line's existing block prefix is replaced by `kind`.
// Bullet/task lines keep their indentation; headings always start at column 0.
EditResult ToggleLinePrefix(const Cells& text, int selA, int selB, LinePrefix kind);

// Enter pressed with the caret at `cursor` (no selection). On a bullet/task line, starts the next
// line with the same prefix (tasks always unchecked). On an EMPTY bullet/task item, removes the
// prefix instead (ends the list). Otherwise handled == false.
EditResult ContinueListOnEnter(const Cells& text, int cursor);

// Apply an EditResult's ops to a plain codepoint sequence (used by tests and as the reference for
// the editor's own cell-based apply).
void ApplyOps(Cells& text, const std::vector<EditOp>& ops);

// UTF-8 <-> Cells convenience for tests.
Cells       ToCells(const std::string& ascii);
std::string FromCells(const Cells& cells);

}} // namespace Magpie::Md
