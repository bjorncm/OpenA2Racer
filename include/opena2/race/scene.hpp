#pragma once

// What the race (A2SPEL.DAT) draws and from where: the camera matrices, the
// view window and view wedge that select the visible objects and road
// segments, and the placement of the static scenery objects.

#include "opena2/race/race_math.hpp"
#include "opena2/race/stage.hpp"
#include "opena2/race/track.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opena2::race {

// FUN_00423553, FUN_004235fb, FUN_0042369d: D3D rotations about x, y and z by
// an angle in 1024ths.
Mat4 rotationX(int angle) noexcept;
Mat4 rotationY(int angle) noexcept;
Mat4 rotationZ(int angle) noexcept;

// camera_record (0x427c2a): the view matrix is translation(-position) times
// the rotation rotY(heading) * rotZ(roll) * rotX(pitch).
Mat4 viewMatrix(const Vec3& position, int pitch, int heading, int roll = 0) noexcept;

// The projection matrix at 0x459598, used as it is:
// x' = 2x, y' = 2y, z' = z - 1, w = z (near plane 1, no far plane).
Mat4 projectionMatrix() noexcept;

// FUN_0041c0d4: the viewport scales x' / w and y' / w by the viewport height
// and clips them at width / (2 * height) and 0.5. So the visible half-extents
// of x / z and y / z are width / (4 * height) and 1/4.
struct ViewportExtent {
    float halfX = 0.0F;
    float halfY = 0.0F;
};
ViewportExtent viewportExtent(int width, int height) noexcept;

// The world rectangle of place_object (0x426c74). Objects inside it are
// visible (check_objects3d_visibility, inclusive) and road segments are tested
// against it (exclusive).
struct ViewWindow {
    int xMin = 0;
    int zMin = 0;
    int xMax = 0;
    int zMax = 0;

    bool contains(float x, float z) const noexcept
    {
        return static_cast<float>(xMin) <= x && x <= static_cast<float>(xMax) &&
               static_cast<float>(zMin) <= z && z <= static_cast<float>(zMax);
    }
};

// The sector 0..7 of the camera follow (FUN_00404c39) from the heading, then
// place_object around the camera position divided by 30.
int viewSector(int heading) noexcept;
// `distance` (the start window's Zichtafstand, 1 is the original) scales the
// window's size; the part behind the camera stays the same.
ViewWindow viewWindow(const Vec3& camera, int heading, float distance = 1.0F) noexcept;

// store_camera_vectors (0x425ec4) and FUN_00425efc: a wedge with its apex 150
// units behind the camera, open 300 units to each side at the camera.
class ViewWedge {
public:
    ViewWedge(const Vec3& camera, int heading) noexcept;
    bool contains(int x, int z) const noexcept;

private:
    int ax_ = 0;
    int az_ = 0;
    int bx_ = 0;
    int bz_ = 0;
    int cx_ = 0;
    int cz_ = 0;
};

// find_weg_element_at (0x41ffce) for segments 0 .. pieces - 2 (the original's
// point count - 3). The returned range is the one FUN_004203ac draws: flagged
// segments from the first flagged one up to, but not including, the last.
struct VisibleSegments {
    std::vector<bool> flags;
    std::size_t first = 0;
    std::size_t last = 0;
};
VisibleSegments findVisibleSegments(std::span<const RoadPiece> pieces,
                                    const ViewWedge& wedge,
                                    const ViewWindow& window);

// road_height_at (0x426eac): the height of piece `piece`'s plane at (x, z).
float roadHeightAt(const RoadPiece& piece, float x, float z) noexcept;

// A static object made by load_static_object (0x407ac4).
struct StaticObject {
    int type = 0;
    Vec3 position;
    int yaw = 0;
};

// FUN_0040a17d: max_x^2 + max_z^2 over the mesh, with the maxima starting at 0.
float squaredRadius(std::span<const Vec3> vertices) noexcept;

// load_static_object(type, piece, mode, side, distance). `squaredRadius` is
// the object's collision value at racer +0x54, which modes 3 and 4 add to the
// distance as it is.
StaticObject placeStaticObject(std::span<const RoadPiece> pieces,
                               const SceneryRecord& record,
                               float squaredRadius) noexcept;

// The road-object callback 0x4080bd: the world matrix is rotY(0x4ff - yaw)
// with the position as translation.
Mat4 staticObjectMatrix(const StaticObject& object) noexcept;

} // namespace opena2::race
