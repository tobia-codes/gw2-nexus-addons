// MarkdownRender.cpp — manual draw-list layout of parsed markdown for VIEW mode.
//
// We do our own word-wrapping inline layout (rather than ImGui::TextWrapped)
// because a single line mixes per-span fonts and styles, and because chips
// will later need custom positioning (icon + name token). A "pen" walks
// left-to-right in screen space; words wrap when they would overrun wrapWidth.
//
// Faux styling (Nexus has no bold/italic font):
//   * Bold   -> draw the word twice, offset by 1px horizontally (thickening).
//   * Italic -> draw once, then shear every emitted vertex horizontally based
//               on its distance above the glyph baseline (slants the tops).
//
// All rendered text is ASCII.

#include "MarkdownRender.h"

#include <cfloat>   // FLT_MAX
#include <string>
#include <vector>

#include "imgui.h"
#include "ChipRich.h"   // Magpie::DrawRichChip / MeasureRichChip (Task 13)

namespace Magpie {

namespace {

// Hanging-indent width for bullets, in "- " worth of pixels (measured live).
// Italic shear factor: ~0.22 slants glyph tops to the right.
constexpr float kItalicShear = 0.22f;
// Small leading added to each line's height.
constexpr float kLeading = 2.0f;

// Split a span's display text into words, each carrying a single trailing
// space (so re-joined spacing is consistent). The final word keeps no
// trailing space only if the source had none; we simply append a space to
// every word and rely on the next span continuing on the same pen — close
// enough for a notes preview and keeps spacing visible between words.
std::vector<std::string> SplitWords(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        // Skip leading spaces but emit them as part of separation: collapse
        // runs of spaces into a single trailing space on the previous word.
        size_t start = i;
        while (i < n && s[i] != ' ') ++i;
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
        // Consume the space run.
        bool hadSpace = false;
        while (i < n && s[i] == ' ') { ++i; hadSpace = true; }
        if (hadSpace && !out.empty()) {
            out.back().push_back(' ');
        } else if (hadSpace && out.empty()) {
            // Leading whitespace-only: represent as a lone space word so the
            // pen advances (rare, but avoids dropping intentional indent).
            out.push_back(std::string(" "));
        }
    }
    return out;
}

// Draw a task checkbox (box + check mark) with its top-left at `pos`. Drawn
// with primitives, not glyphs: Nexus's font has no ballot-box characters.
void DrawCheckbox(ImDrawList* dl, const ImVec2& pos, float size, bool checked, bool hovered)
{
    const ImVec2 pMax(pos.x + size, pos.y + size);
    const ImU32 frame = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    dl->AddRectFilled(pos, pMax, frame, 2.0f);
    dl->AddRect(pos, pMax, hovered ? ImGui::GetColorU32(ImGuiCol_CheckMark) : border, 2.0f);
    if (checked) {
        const ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const float pad = size * 0.22f;
        const float th  = size > 12.0f ? 2.0f : 1.5f;
        const ImVec2 a(pos.x + pad,               pos.y + size * 0.52f);
        const ImVec2 b(pos.x + size * 0.42f,      pMax.y - pad);
        const ImVec2 c(pMax.x - pad,              pos.y + pad);
        dl->AddLine(a, b, mark, th);
        dl->AddLine(b, c, mark, th);
    }
}

// Draw a word with the given font/size/colour at pos. style: 0=plain,
// 1=bold, 2=italic. Returns nothing; caller advances the pen by measured width.
void DrawWord(ImDrawList* dl, ImFont* font, float fontSize,
              const ImVec2& pos, ImU32 col, const char* text, int style)
{
    if (style == 1) {
        // Bold: double-draw, second copy nudged 1px right to thicken strokes.
        dl->AddText(font, fontSize, pos, col, text);
        dl->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), col, text);
        return;
    }
    if (style == 2) {
        // Italic: record vertex range, draw, then shear the new vertices.
        const int vtxStart = dl->VtxBuffer.Size;
        dl->AddText(font, fontSize, pos, col, text);
        const int vtxEnd = dl->VtxBuffer.Size;
        const float baseY = pos.y + fontSize;  // glyph baseline-ish (bottom)
        for (int v = vtxStart; v < vtxEnd; ++v) {
            ImDrawVert& vert = dl->VtxBuffer[v];
            vert.pos.x += (baseY - vert.pos.y) * kItalicShear;
        }
        return;
    }
    // Plain text.
    dl->AddText(font, fontSize, pos, col, text);
}

} // namespace

int RenderMarkdown(const std::vector<Md::Line>& lines, ImFont* fontBig, float wrapWidth,
                   bool interactiveTasks)
{
    if (wrapWidth <= 0.0f) wrapWidth = 1.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 doneCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    int clickedTask = -1;

    const float rightEdge = origin.x + wrapWidth;

    float penX = origin.x;
    float penY = origin.y;
    float maxPenY = origin.y;
    // ImGui::GetFontSize() includes the window font scale (text-size setting) and any global
    // scale; font->FontSize alone is the unscaled atlas size. Apply the same factor to FontBig.
    const float baseSize  = ImGui::GetFontSize();
    const float fontScale = baseSize / ImGui::GetFont()->FontSize;
    float lastLineHeight = baseSize + kLeading;

    // Running per-chip id so each chip's right-click popup has a unique scope.
    int chipUid = 0;

    for (int lineIdx = 0; lineIdx < (int)lines.size(); ++lineIdx) {
        const Md::Line& line = lines[lineIdx];
        // Choose active font.
        ImFont* font = ImGui::GetFont();
        float fontSize = baseSize;
        if (line.block == Md::Block::Heading && fontBig != nullptr) {
            font = fontBig;
            fontSize = fontBig->FontSize * fontScale;
        }
        const float lineHeight = fontSize + kLeading;
        lastLineHeight = lineHeight;

        // Compute this line's left edge (hanging indent for bullets).
        float startX = origin.x;
        penX = startX;

        if (line.block == Md::Block::Bullet) {
            const char* marker = "- ";
            const ImVec2 mSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, marker);
            dl->AddText(font, fontSize, ImVec2(penX, penY), textCol, marker);
            penX += mSize.x;
            startX += mSize.x;  // continuation lines hang under the text
        }

        // Checked tasks: dimmed text with a strike-through.
        const bool done = (line.block == Md::Block::Task && line.checked);
        const ImU32 lineCol = done ? doneCol : textCol;

        if (line.block == Md::Block::Task) {
            const float box = fontSize * 0.85f;
            const ImVec2 bMin(penX + 1.0f, penY + (fontSize - box) * 0.5f);
            const ImVec2 bMax(bMin.x + box, bMin.y + box);
            bool hovered = false;
            if (interactiveTasks) {
                // Generous hit area: the full line height around the box. Only when
                // this window is the hovered one (never through a popup).
                hovered = ImGui::IsWindowHovered() &&
                          ImGui::IsMouseHoveringRect(ImVec2(bMin.x - 2.0f, penY),
                                                     ImVec2(bMax.x + 4.0f, penY + lineHeight));
                if (hovered) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        clickedTask = lineIdx;
                }
            }
            DrawCheckbox(dl, bMin, box, line.checked, hovered);
            const float adv = box + fontSize * 0.45f;
            penX += adv;
            startX += adv;      // continuation lines hang under the text
        }

        // Lay out spans.
        for (const Md::Span& span : line.spans) {
            if (span.kind == Md::Inline::Chip) {
                // Measure chip width first (same computation DrawRichChip uses)
                // so the wrap decision matches the draw.
                const float chipW = MeasureRichChip(font, fontSize, span.text);
                if (penX + chipW > rightEdge && penX > startX) {
                    penX = startX;
                    penY += lineHeight;
                }
                const float w = DrawRichChip(dl, font, fontSize, ImVec2(penX, penY),
                                             span.text, chipUid++);
                penX += w;
                continue;
            }

            int style = 0;
            if (span.kind == Md::Inline::Bold)   style = 1;
            else if (span.kind == Md::Inline::Italic) style = 2;

            const std::vector<std::string> words = SplitWords(span.text);
            for (const std::string& word : words) {
                const float wordW =
                    font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, word.c_str()).x;

                // Wrap before drawing, but only if there is already content on
                // this visual line (penX past start). A single word wider than
                // wrapWidth is drawn anyway (no infinite loop / no hang).
                if (penX + wordW > rightEdge && penX > startX) {
                    penX = startX;
                    penY += lineHeight;
                }

                DrawWord(dl, font, fontSize, ImVec2(penX, penY), lineCol,
                         word.c_str(), style);
                if (done) {
                    // Strike-through including the inter-word space, so it reads as one line.
                    const float y = penY + fontSize * 0.55f;
                    dl->AddLine(ImVec2(penX, y), ImVec2(penX + wordW, y), lineCol, 1.0f);
                }
                penX += wordW;
            }
        }

        // End of source line: advance to the next line.
        if (penY > maxPenY) maxPenY = penY;
        penY += lineHeight;
    }

    if (penY > maxPenY) maxPenY = penY;

    // Reserve vertical space so ImGui scrolling/layout accounts for our draws.
    const float h = (maxPenY + lastLineHeight) - origin.y;
    ImGui::Dummy(ImVec2(wrapWidth, h));
    return clickedTask;
}

} // namespace Magpie
