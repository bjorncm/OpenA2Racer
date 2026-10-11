#pragma once

// The moving part of a racer record of A2SPEL.DAT (DAT_00473008, 0x100 bytes
// per car) that the player and the computer cars share, and the collision
// code of opponent.cpp: the road walls and track ends, and car against car.

#include "opena2/race/race_math.hpp"
#include "opena2/race/track.hpp"

#include <array>
#include <span>
#include <vector>

namespace opena2::race {

// FUN_0040a17d: a car's box in its own frame, from the mesh's x/z extent.
struct CarBox {
    // x_min,z_min / x_max,z_min / x_max,z_max / x_min,z_max (record +0x88).
    std::array<Vec3, 4> corners{};
    // max_x^2 + max_z^2 (record +0x54).
    float radiusSquared = 0.0F;

    static CarBox fromVertices(std::span<const Vec3> vertices) noexcept;
};

struct CarBody {
    Vec3 position;             // +0x04
    float speed = 0.0F;        // +0x10
    // (cos, 0, sin) of heading + 0x100 (+0x18..+0x20).
    Vec3 direction;
    // 1024ths, not wrapped (+0x24).
    int heading = 0;
    float mass = 0.0F;         // +0x30
    CarBox box;                // +0x54, +0x88
    // World corners (+0x58).
    std::array<Vec3, 4> corners{};
    // Road piece (+0xc4) and lane 0..10 or -1 (+0xc8).
    int piece = 0;
    int lane = -1;
    // +0xd8: 1 drives toward higher pieces.
    bool forward = true;
    // Bounce velocity (+0xec, +0xf0) and this frame's movement (+0xf4, +0xf8).
    Vec3 bounce;
    Vec3 step;

    // opponent_corners_at_heading (0x40bccd).
    void updateCorners() noexcept;
    // The direction and the step of init_player_opponent's movement pass.
    void startStep() noexcept;
};

// FUN_004131a8: the side walls 120 and 5 units from the centre line shorten
// the step and slow the car to 0.845. True when hit; `bump` when the car is
// still faster than 4 (FUN_004105b4, bump3.wav at the car).
bool hitRoadWalls(std::span<const RoadPiece> pieces, CarBody& car, bool& bump);
// FUN_004133d1 (the player only): near either end of the track the car stays
// between 1 and 59 units along its piece.
bool hitTrackEnds(std::span<const RoadPiece> pieces, CarBody& car, bool& bump);

// sound_attenuation_a (0x410527) from the bounce: bump2.wav at the car, its
// volume lowered by 25 per unit of speed.
struct Crash {
    Vec3 position;
    float speed = 0.0F;
};

namespace contact {
constexpr int none = 0;
// Within the radius test, which compares squared radii summed and squared
// again, so it reaches far.
constexpr int nearby = 1;
constexpr int hit = 2;
} // namespace contact

// FUN_0040a895(a, b, apply): car `a` moved by its step against car `b` where
// it stands. A corner or the centre of either inside the other is a hit; with
// `apply` every hit bounces (FUN_00409cfc) and the test goes on.
int collideCars(CarBody& a, CarBody& b, bool apply, std::vector<Crash>* crashes = nullptr);

} // namespace opena2::race
