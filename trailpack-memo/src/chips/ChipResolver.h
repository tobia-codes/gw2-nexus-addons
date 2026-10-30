#pragma once
#include <string>
#include <cstdint>

namespace Trailpack {

// The view of a chip used by both the markdown renderer and the chip text editor.
// Structural tier (Task 9): label + colour derived purely from the chat-link codec,
// no network/service. Task 13 adds resolved name / icon / tooltip fields additively.
struct ChipView {
    std::string label;   // display label, e.g. "[Item]", "[Waypoint]", "[Mesmer Build]"
    uint32_t    color;   // packed ABGR (ImU32-compatible: IM_COL32 layout) tint for the chip
};

// Resolve a raw "[&...]" chat code to its structural view (offline; no Trailpack Core).
ChipView ResolveChip(const std::string& chatCode);

} // namespace Trailpack
