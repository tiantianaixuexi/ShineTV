#include "ui/imgui/kit/Icon_Draw.h"

#include "ui/imgui/kit/Icon_Registry.h"

#include <cmath>

namespace shine::kit {

void DrawIcon(ImDrawList* draw, std::string_view name, ImVec2 pos, float size, ImU32 color,
              float thickness) {
    const IconGlyph* glyph = GetIcon(name);
    if (glyph == nullptr) {
        return;
    }
    const float scale = size / 24.0f;
    const float width = std::fmax(1.0f, thickness * scale);
    for (const IconSubPath& sub : glyph->subPaths) {
        if (sub.segments.empty()) {
            continue;
        }
        draw->PathClear();
        draw->PathLineTo(ImVec2(pos.x + sub.start.x * scale, pos.y + sub.start.y * scale));
        for (const IconSegment& segment : sub.segments) {
            if (segment.op == 'L') {
                draw->PathLineTo(ImVec2(pos.x + segment.p[0] * scale, pos.y + segment.p[1] * scale));
            } else if (segment.op == 'C') {
                draw->PathBezierCubicCurveTo(
                    ImVec2(pos.x + segment.p[0] * scale, pos.y + segment.p[1] * scale),
                    ImVec2(pos.x + segment.p[2] * scale, pos.y + segment.p[3] * scale),
                    ImVec2(pos.x + segment.p[4] * scale, pos.y + segment.p[5] * scale));
            }
        }
        draw->PathStroke(color, 0, width);
    }
}

void DrawIconCentered(ImDrawList* draw, std::string_view name, ImVec2 center, float size,
                      ImU32 color, float thickness) {
    DrawIcon(draw, name, ImVec2(center.x - size * 0.5f, center.y - size * 0.5f), size, color,
             thickness);
}

} // namespace shine::kit
