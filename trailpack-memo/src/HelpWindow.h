#pragma once
// HelpWindow.h — the in-game guide ("?" in the title bar) for Trailpack Memo.

namespace Help {

// Draw a small round "?" button in the CURRENT window's title bar, right after its title
// text. Call directly after ImGui::Begin (before any font scaling). Toggles *open.
void TitleBarButton(const char* windowTitle, bool* open);

// The guide window: collapsible sections covering every feature. No-op when !*open.
void Render(bool* open, float uiScale);

} // namespace Help
