#pragma once
// MarkdownRender.h — ImGui draw-list rendering of parsed markdown (VIEW mode).
//
// Renders the subset of markdown produced by Trailpack::Md::Parse: headings,
// bullets, task checkboxes, paragraphs, with inline bold (faux via double-draw) and italic
// (faux via per-glyph vertex shear), since Nexus exposes no bold/italic font.
// Chips render as a placeholder token for now (Task 13 replaces with real
// icon + resolved name).

#include <vector>
#include "Markdown.h"

struct ImFont;  // forward (defined in imgui.h)

namespace Trailpack {

// Render parsed markdown lines at the current ImGui cursor, wrapping to
// `wrapWidth` pixels. `fontBig` is Nexus's FontBig for headings (may be null
// -> falls back to the current font). Advances ImGui layout (via Dummy) to
// cover the drawn height so the content participates in scrolling.
//
// Task lines draw a checkbox. When `interactiveTasks` is true, a left click on
// a box is reported: the return value is the index (into `lines`, == source
// line) of the clicked task, or -1. The caller performs the toggle.
int RenderMarkdown(const std::vector<Md::Line>& lines, ImFont* fontBig, float wrapWidth,
                   bool interactiveTasks = false);

// Chips in checked tasks (Options > "Events", set by dllmain from settings.json):
// event countdown kept (in a much lighter colour) / tooltips + right-click menu kept.
void SetCheckedTaskChips(bool showTime, bool popups);

} // namespace Trailpack
