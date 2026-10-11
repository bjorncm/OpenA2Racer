#include "opena2/screen_transform.hpp"

#include <algorithm>
#include <cmath>

namespace opena2 {

ScreenTransform ScreenTransform::fit(int targetWidth, int targetHeight) noexcept
{
    ScreenTransform transform;
    if (targetWidth <= 0 || targetHeight <= 0) {
        return transform;
    }
    const auto width = static_cast<float>(targetWidth);
    const auto height = static_cast<float>(targetHeight);
    transform.scale = std::min(width / logicalWidth, height / logicalHeight);
    transform.offsetX = (width - logicalWidth * transform.scale) * 0.5F;
    transform.offsetY = (height - logicalHeight * transform.scale) * 0.5F;
    return transform;
}

int ScreenTransform::toTargetX(float x) const noexcept
{
    return static_cast<int>(std::lround(offsetX + x * scale));
}

int ScreenTransform::toTargetY(float y) const noexcept
{
    return static_cast<int>(std::lround(offsetY + y * scale));
}

PixelRect ScreenTransform::toTarget(float x, float y, float width, float height) const noexcept
{
    return {toTargetX(x), toTargetY(y), toTargetX(x + width), toTargetY(y + height)};
}

std::pair<float, float> ScreenTransform::toLogical(float targetX, float targetY) const noexcept
{
    return {(targetX - offsetX) / scale, (targetY - offsetY) / scale};
}

} // namespace opena2
