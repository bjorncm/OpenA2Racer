#pragma once

#include <utility>

namespace opena2 {

struct PixelRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

// Maps the menu's logical 640x480 coordinate space onto a target surface
// (window pixels or window points), centred and letterboxed without
// stretching. Everything is rasterized at the target resolution; logical
// coordinates only describe layout.
struct ScreenTransform {
    static constexpr float logicalWidth = 640.0F;
    static constexpr float logicalHeight = 480.0F;

    float scale = 1.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;

    static ScreenTransform fit(int targetWidth, int targetHeight) noexcept;

    // Edges are rounded independently so adjacent rectangles stay seamless.
    PixelRect toTarget(float x, float y, float width, float height) const noexcept;
    int toTargetX(float x) const noexcept;
    int toTargetY(float y) const noexcept;
    std::pair<float, float> toLogical(float targetX, float targetY) const noexcept;
};

} // namespace opena2
