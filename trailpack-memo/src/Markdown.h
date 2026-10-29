#pragma once
#include <string>
#include <vector>

namespace Magpie { namespace Md {

enum class Block  { Paragraph, Heading, Bullet, Task };
enum class Inline { Text, Bold, Italic, Chip };

struct Span {
    Inline      kind;
    std::string text;   // literal display text (markers stripped) for Text/Bold/Italic;
                        // the raw "[&...]" chat code for Chip
};

struct Line {
    Block             block        = Block::Paragraph;
    int               headingLevel = 0;      // # count for Heading (1..N); 0 otherwise
    bool              checked      = false;  // Task only: "- [x]" / "- [X]"
    std::vector<Span> spans;
};

// Split body on '\n', one Line per source line (so Line index == source line index).
// Empty body -> empty vector.
std::vector<Line> Parse(const std::string& body);

// ── Task lists (GFM "- [ ] " / "- [x] ") ──────────────────────────────────
// All helpers address lines by 0-based source line index, matching Parse().

struct TaskCount {
    int done  = 0;
    int total = 0;
};

// Count task lines and how many of them are checked.
TaskCount CountTasks(const std::string& body);

// Flip the checkbox on source line `lineIndex`. Returns false (body untouched)
// if that line does not exist or is not a task line.
bool ToggleTask(std::string& body, int lineIndex);

// Set every task line to unchecked ("[ ]"). Returns the number of lines changed.
int UncheckAllTasks(std::string& body);

}} // namespace Magpie::Md
