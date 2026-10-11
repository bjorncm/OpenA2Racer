#include "opena2/race/scene.hpp"

#include <algorithm>
#include <cmath>

namespace opena2::race {
namespace {

// The window of place_object: the map defaults at 0x4596d8.. (the ground map
// is never initialised, so they stay): 6 x 6 tiles of 12 units, a map of
// 200000 x 200000, and 30 world units per unit.
constexpr int windowTiles = 6;
constexpr int tileSize = 12;
constexpr int mapSize = 200000;
constexpr double windowUnit = 30.0;

int roundToInt(double value) noexcept
{
    return static_cast<int>(std::nearbyint(value));
}

// FUN_00425e78: the sign of the cross product of (p - from) and (to - from),
// in the original's 32-bit integer arithmetic.
int sideOfLine(int fromX, int fromZ, int toX, int toZ, int x, int z) noexcept
{
    const auto product = [](std::int64_t a, std::int64_t b) {
        return static_cast<std::uint32_t>(a * b);
    };
    const auto value = static_cast<std::int32_t>(product(x - fromX, toZ - fromZ) -
                                                 product(toX - fromX, z - fromZ));
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

constexpr double yawScale = 1024.0 / 6.283185307;

int yawFromAtan(double y, double x) noexcept
{
    return roundToInt(std::atan2(y, x) * yawScale);
}

} // namespace

Mat4 rotationX(int angle) noexcept
{
    const float c = angleCos(angle);
    const float s = angleSin(angle);
    Mat4 r = Mat4::identity();
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}

Mat4 rotationY(int angle) noexcept
{
    const float c = angleCos(angle);
    const float s = angleSin(angle);
    Mat4 r = Mat4::identity();
    r.m[0][0] = c;
    r.m[0][2] = -s;
    r.m[2][0] = s;
    r.m[2][2] = c;
    return r;
}

Mat4 rotationZ(int angle) noexcept
{
    const float c = angleCos(angle);
    const float s = angleSin(angle);
    Mat4 r = Mat4::identity();
    r.m[0][0] = c;
    r.m[0][1] = s;
    r.m[1][0] = -s;
    r.m[1][1] = c;
    return r;
}

Mat4 viewMatrix(const Vec3& position, int pitch, int heading, int roll) noexcept
{
    // mat4_multiply(a, b) stores b * a in a: each rotation is applied on the left.
    const Mat4 rotation = rotationY(heading) * (rotationZ(roll) * rotationX(pitch));
    return Mat4::translation(position * -1.0F) * rotation;
}

Mat4 projectionMatrix() noexcept
{
    Mat4 r;
    r.m[0][0] = 2.0F;
    r.m[1][1] = 2.0F;
    r.m[2][2] = 1.0F;
    r.m[2][3] = 1.0F;
    r.m[3][2] = -1.0F;
    return r;
}

ViewportExtent viewportExtent(int width, int height) noexcept
{
    // Clip limits width / (2 * height) and 0.5 on 2x / z and 2y / z.
    const float h = static_cast<float>(std::max(height, 1));
    return {static_cast<float>(width) / (4.0F * h), 0.25F};
}

int viewSector(int heading) noexcept
{
    const double c = angleCos(heading + 0x100);
    const double s = angleSin(heading + 0x100);
    if (c >= -0.5) {
        if (c <= 0.5) {
            if (s < -0.5) {
                return 4;
            }
            return s <= 0.5 ? 1 : 0;
        }
        if (s < -0.5) {
            return 3;
        }
        return s <= 0.5 ? 2 : 1;
    }
    if (s < -0.5) {
        return 5;
    }
    return s <= 0.5 ? 6 : 7;
}

ViewWindow viewWindow(const Vec3& camera, int heading, float distance) noexcept
{
    const int x = roundToInt(camera.x / windowUnit);
    const int z = roundToInt(camera.z / windowUnit);
    constexpr int back = 2 * tileSize; // 24
    // 72 in the original; an even number, so span / 2 stays exact.
    const int span = std::max(back + 2, 2 * roundToInt(windowTiles * tileSize * std::max(distance, 0.0F) / 2.0));
    int x0 = 0;
    int x1 = 0;
    int z0 = 0;
    int z1 = 0;
    switch (viewSector(heading)) {
    case 0:
        x0 = x - span / 2, x1 = x + span / 2, z0 = z - back, z1 = z + span - back;
        break;
    case 1:
        x0 = x - back, x1 = x + span - back, z0 = z - back, z1 = z + span - back;
        break;
    case 2:
        x0 = x - back, x1 = x + span - back, z0 = z - span / 2, z1 = z + span / 2;
        break;
    case 3:
        x0 = x - back, x1 = x + span - back, z0 = z - span + back, z1 = z + back;
        break;
    case 4:
        x0 = x - span / 2, x1 = x + span / 2, z0 = z - span + back, z1 = z + back;
        break;
    case 5:
        x0 = x - span + back, x1 = x + back, z0 = z - span + back, z1 = z + back;
        break;
    case 6:
        x0 = x - span + back, x1 = x + back, z0 = z - span / 2, z1 = z + span / 2;
        break;
    default:
        x0 = x - span + back, x1 = x + back, z0 = z - back, z1 = z + span - back;
        break;
    }
    if (x0 < 0) {
        x0 = 0, x1 = span;
    }
    if (z0 < 0) {
        z0 = 0, z1 = span;
    }
    if (z1 > mapSize) {
        z0 = mapSize - span, z1 = mapSize;
    }
    if (x1 > mapSize) {
        x0 = mapSize - span, x1 = mapSize;
    }
    return {roundToInt(x0 * windowUnit), roundToInt(z0 * windowUnit),
            roundToInt(x1 * windowUnit), roundToInt(z1 * windowUnit)};
}

ViewWedge::ViewWedge(const Vec3& camera, int heading) noexcept
{
    // Constants 30.0, 5.0 and 10.0 (_DAT_00441cf7, _DAT_00441d0f, _DAT_00441d17).
    const double side = windowUnit * 10.0;
    const double back = windowUnit * 5.0;
    const double c = angleCos(heading);
    const double s = angleSin(heading);
    ax_ = roundToInt(camera.x + c * side);
    az_ = roundToInt(camera.z + s * side);
    bx_ = roundToInt(camera.x + angleCos(heading - 0x100) * back);
    bz_ = roundToInt(camera.z + angleSin(heading - 0x100) * back);
    cx_ = roundToInt(camera.x - c * side);
    cz_ = roundToInt(camera.z - s * side);
}

bool ViewWedge::contains(int x, int z) const noexcept
{
    return sideOfLine(cx_, cz_, bx_, bz_, x, z) < 0 && sideOfLine(bx_, bz_, ax_, az_, x, z) < 0;
}

VisibleSegments findVisibleSegments(std::span<const RoadPiece> pieces,
                                    const ViewWedge& wedge,
                                    const ViewWindow& window)
{
    // The lane vertex tested is entry 10 of each verge array (chunk +0x2c / +0x30, offset 0x28).
    constexpr std::size_t laneSample = 10;
    const auto inWedge = [&wedge](const Vec3& p) {
        return wedge.contains(roundToInt(p.x), roundToInt(p.z));
    };
    const auto inWindow = [&window](const Vec3& p) {
        return static_cast<float>(window.xMin) < p.x && p.x < static_cast<float>(window.xMax) &&
               static_cast<float>(window.zMin) < p.z && p.z < static_cast<float>(window.zMax);
    };

    VisibleSegments result;
    const std::size_t count = pieces.size() >= 2 ? pieces.size() - 1 : 0;
    result.flags.assign(count, false);
    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const RoadPiece& piece = pieces[i];
        const Vec3* right = piece.rightVerge.size() > laneSample ? &piece.rightVerge[laneSample].position : nullptr;
        const Vec3* left = piece.leftVerge.size() > laneSample ? &piece.leftVerge[laneSample].position : nullptr;
        if (!inWedge(piece.centre.position) && !(right && inWedge(*right)) && !(left && inWedge(*left))) {
            continue;
        }
        if (inWindow(piece.centre.position) || inWindow(piece.right.position) ||
            inWindow(piece.left.position) || (right && inWindow(*right)) || (left && inWindow(*left))) {
            result.flags[i] = true;
            result.last = i;
            if (first) {
                first = false;
                result.first = i;
            }
        }
    }
    return result;
}

float roadHeightAt(const RoadPiece& piece, float x, float z) noexcept
{
    return -(piece.up.z * z + piece.up.x * x + piece.plane) / piece.up.y;
}

float squaredRadius(std::span<const Vec3> vertices) noexcept
{
    float maxX = 0.0F;
    float maxZ = 0.0F;
    for (const auto& v : vertices) {
        maxX = std::max(maxX, v.x);
        maxZ = std::max(maxZ, v.z);
    }
    return maxZ * maxZ + maxX * maxX;
}

StaticObject placeStaticObject(std::span<const RoadPiece> pieces,
                               const SceneryRecord& record,
                               float radiusSquared) noexcept
{
    StaticObject object;
    object.type = record.type;
    if (record.piece < 0 || static_cast<std::size_t>(record.piece) >= pieces.size()) {
        return object;
    }
    const RoadPiece& piece = pieces[static_cast<std::size_t>(record.piece)];
    const Vec3 base = piece.centre.position;
    const Vec3 side = normalise(Vec3{piece.side.x, 0.0F, piece.side.z});
    const float distance = static_cast<float>(record.distance);
    // Constant 2.0 at 0x442f5a.
    const float outer = radiusSquared + distance * 2.0F;

    switch (record.mode) {
    case 0:
        object.position = {base.x, roadHeightAt(piece, base.x, base.z), base.z};
        break;
    case 1:
    case 2: {
        const float sign = record.mode == 1 ? -1.0F : 1.0F;
        const float x = base.x + sign * side.x * distance;
        const float z = base.z + sign * side.z * distance;
        object.position = {x, roadHeightAt(piece, x, z), z};
        break;
    }
    case 3:
        object.position = {base.x - outer * side.x, 0.0F, base.z - outer * side.z};
        break;
    case 4:
        object.position = {base.x + outer * side.x, 0.0F, base.z + outer * side.z};
        break;
    default:
        break;
    }

    // The yaw: atan2 of the side vector, turned by the `side` argument and
    // the mode, with fixed values when the deciding component is exactly 0.
    const bool outward = record.mode == 1 || record.mode == 3;
    const double sx = side.x;
    const double sz = side.z;
    if (record.side == 0) {
        if (sx == 0.0) {
            object.yaw = outward ? 0 : 0x200;
        } else {
            object.yaw = outward ? yawFromAtan(sz, sx) : yawFromAtan(-sz, -sx);
        }
    } else if (record.side == 1) {
        if (sz == 0.0) {
            object.yaw = outward ? 0x312 : 0x100;
        } else {
            object.yaw = outward ? yawFromAtan(sx, -sz) : yawFromAtan(-sx, sz);
        }
    }
    return object;
}

Mat4 staticObjectMatrix(const StaticObject& object) noexcept
{
    const int angle = (0x3ff - (object.yaw & 0x3ff)) + 0x100;
    return rotationY(angle) * Mat4::translation(object.position);
}

} // namespace opena2::race
