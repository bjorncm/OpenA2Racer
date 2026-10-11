#pragma once

// The track pickups and roadworks of A2SPEL.DAT: the Time, Fuel and Money
// bonus objects (bonus.cpp) and the pions, the work-zone cones with their
// speed camera (pion.cpp).
//
// Placement draws from the race's lcg_rand in the original's order; the
// object callbacks and updates are one call per original frame.

#include "opena2/race/race_math.hpp"
#include "opena2/race/track.hpp"

#include <span>
#include <vector>

namespace opena2::race {

namespace bonus_type {
constexpr int none = 0;
constexpr int time = 1;
constexpr int fuel = 2;
constexpr int money = 3;
} // namespace bonus_type

// A bonus record (0x19 bytes at DAT_00475ab8) with its object.
struct BonusObject {
    // Type 0 once taken; the object is then hidden.
    int type = bonus_type::none;
    int piece = -1;
    int lane = -1;
    // Static object type 26 tijd, 25 benzine or 24 geld.
    int objectType = 0;
    Vec3 position;
    // Object +0x3f: the turn the callback applies each frame.
    int yaw = 0;
    // The object's matrix; the callback turns the previous one further.
    Mat4 matrix = Mat4::identity();
};

// A pion record (0x2c bytes at DAT_00475ad8) with its object.
struct PionObject {
    // Record +0x18: in use. A pion that fell below height 0 is retired.
    bool active = true;
    // Static object type 19 werkweg2 (cone) or 20 flits (speed camera).
    int objectType = 0;
    int piece = -1;
    int lane = -1;
    // Hit: flying and tumbling.
    bool hit = false;
    Vec3 position;
    Vec3 velocity;
    // Object +0x3b, +0x3f, +0x43: the turns about x, y and z.
    int angleX = 0;
    int angleY = 0;
    int angleZ = 0;
    Mat4 matrix = Mat4::identity();
};

// What the player's car gives the pion update: its piece and lane (-1 when
// off the lanes) and its velocity, direction times speed.
struct PionHitter {
    int piece = -1;
    int lane = -1;
    Vec3 position;
    Vec3 velocity;
};

class Pickups {
public:
    // random_pion_chunk (0x41158d): 4 or 5 roadworks, at pieces
    // round(c + k * 1.5 * c) with c = segments / 20 (FUN_00413dca each).
    void placeRoadworks(std::span<const RoadPiece> pieces, bool forward, Lcg& random);
    // init_bonus_objects (0x412ac2): segments / 27 bonuses from piece 20 on.
    void placeBonuses(std::span<const RoadPiece> pieces, bool forward, Lcg& random);

    // The object callbacks (0x412e6a, 0x4140f4), run for every object each
    // frame before drawing: bonuses turn, pions move by their velocity.
    void runCallbacks();
    // bonus_type_lookup (0x412918) for the player: the bonus on the piece
    // and lane is taken and its type returned; 0 when none.
    int takeBonus(int piece, int lane);
    // update_pions (0x413a5c) for one car. Returns true when a pion was hit
    // for the first time, with its position in `bumpAt` (FUN_00410626).
    bool updatePions(const PionHitter& car, Lcg& random, Vec3& bumpAt);

    std::span<const BonusObject> bonuses() const noexcept { return bonuses_; }
    std::span<const PionObject> pions() const noexcept { return pions_; }

private:
    void addBonus(std::span<const RoadPiece> pieces, int type, int piece, int lane);
    void addPion(std::span<const RoadPiece> pieces, int objectType, int piece, int lane);
    void placeRoadwork(std::span<const RoadPiece> pieces, bool forward, int piece, Lcg& random);

    std::vector<BonusObject> bonuses_;
    std::vector<PionObject> pions_;
};

} // namespace opena2::race
