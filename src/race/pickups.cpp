#include "opena2/race/pickups.hpp"

#include "opena2/race/player_car.hpp"
#include "opena2/race/scene.hpp"
#include "opena2/race/stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace opena2::race {
namespace {

// alloc_pion_records: room for 60 pions.
constexpr std::size_t maxPions = 60;
// The 40.0 at 0x446520, 0x446980 and 0x446a11: along the piece.
constexpr float placeAhead = 40.0F;

int clampPiece(int piece, std::size_t count) noexcept
{
    return std::clamp(piece, 0, static_cast<int>(count) - 1);
}

// The position of a pickup or pion: the lane point at the road height,
// rounded to whole units (the x87 ROUND before start_static_object and
// create_pion).
Vec3 placedOnRoad(std::span<const RoadPiece> pieces, int piece, int lane)
{
    const RoadPiece& road = pieces[static_cast<std::size_t>(piece)];
    Vec3 position = lanePosition(road, lane, placeAhead);
    position.y = std::nearbyint(roadHeightAt(road, position.x, position.z));
    return position;
}

void setRow3(Mat4& matrix, const Vec3& position) noexcept
{
    matrix.m[3][0] = position.x;
    matrix.m[3][1] = position.y;
    matrix.m[3][2] = position.z;
    matrix.m[3][3] = 1.0F;
}

float times095(float value) noexcept
{
    // _DAT_00446a75, a double.
    return static_cast<float>(static_cast<double>(value) * 0.95);
}

} // namespace

void Pickups::addBonus(std::span<const RoadPiece> pieces, int type, int piece, int lane)
{
    // set_bonus_object (0x412737).
    BonusObject bonus;
    bonus.type = type;
    bonus.piece = piece;
    bonus.lane = lane;
    bonus.objectType = type == bonus_type::time ? static_object::time
        : type == bonus_type::fuel             ? static_object::fuel
                                               : static_object::money;
    bonus.position = placedOnRoad(pieces, piece, lane);
    bonus.yaw = 0x100 - pieces[static_cast<std::size_t>(piece)].heading;
    bonuses_.push_back(bonus);
}

void Pickups::addPion(std::span<const RoadPiece> pieces, int objectType, int piece, int lane)
{
    // FUN_00413646 and FUN_004137dc; create_pion sets the height + 0.1
    // (_DAT_00446c79).
    if (pions_.size() >= maxPions) {
        return;
    }
    PionObject pion;
    pion.objectType = objectType;
    pion.piece = piece;
    pion.lane = lane;
    pion.position = placedOnRoad(pieces, piece, lane);
    pion.position.y += 0.1F;
    pions_.push_back(pion);
}

void Pickups::placeRoadwork(std::span<const RoadPiece> pieces, bool forward, int piece,
                            Lcg& random)
{
    // FUN_00413dca: the speed camera one piece before, then ten cones in
    // the outer lane on the driving side.
    if (forward) {
        addPion(pieces, static_object::speedCamera, std::max(piece - 1, 0), 0);
    } else {
        addPion(pieces, static_object::speedCamera, clampPiece(piece + 1, pieces.size()), 10);
    }
    for (int k = 0; k < 10; ++k) {
        const int r = random.next() % 2;
        if (forward) {
            addPion(pieces, static_object::roadworks2, clampPiece(piece + k, pieces.size()), r);
        } else {
            addPion(pieces, static_object::roadworks2, clampPiece(piece - k, pieces.size()),
                    r == 0 ? 10 : 9);
        }
    }
}

void Pickups::placeRoadworks(std::span<const RoadPiece> pieces, bool forward, Lcg& random)
{
    if (pieces.empty()) {
        return;
    }
    const int count = random.next() % 2 + 4;
    const int step = static_cast<int>(pieces.size()) / 20;
    int offset = 0;
    for (int k = 0; k < count; ++k) {
        // _DAT_00446178 = 1.5.
        const int piece = static_cast<int>(
            std::nearbyint(static_cast<double>(step) + static_cast<double>(offset) * 1.5));
        offset += step;
        placeRoadwork(pieces, forward, piece, random);
    }
}

void Pickups::placeBonuses(std::span<const RoadPiece> pieces, bool forward, Lcg& random)
{
    const int segments = static_cast<int>(pieces.size());
    const int count = segments / 27;
    if (count <= 0) {
        return;
    }
    const int step = (segments - 40) / count;
    // EDX after FUN_004126a8, which was given 20 and keeps EDX.
    int lastFuel = 20;
    int base = 20;
    for (int i = 0; i < count; ++i, base += step) {
        const int piece = base - random.next() % 10;
        int lane = random.next() % 4;
        if (lane == 3) {
            lane = 2;
        }
        lane += 1;
        if (!forward) {
            lane = 10 - lane;
        }
        int type = random.next() % 4;
        if (type == 0) {
            type = bonus_type::money;
        }
        // Fuel is never more than 70 pieces apart.
        if (type != bonus_type::fuel && std::abs(piece - lastFuel) > 70) {
            type = bonus_type::fuel;
        }
        if (type == bonus_type::fuel) {
            lastFuel = piece;
        }
        if (piece >= 0 && piece < segments) {
            addBonus(pieces, type, piece, lane);
        }
    }
}

void Pickups::runCallbacks()
{
    // 0x412e6a: the last matrix with the position, turned by the yaw; the
    // yaw then grows by 15.
    for (auto& bonus : bonuses_) {
        if (bonus.type == bonus_type::none) {
            continue;
        }
        setRow3(bonus.matrix, bonus.position);
        bonus.matrix = rotationY(bonus.yaw) * bonus.matrix;
        bonus.yaw = (bonus.yaw + 15) % 0x400;
    }
    // 0x4140f4: move by the velocity, then turn about y, x and z by the
    // angles that are not 0.
    for (auto& pion : pions_) {
        if (!pion.active) {
            continue;
        }
        pion.position = pion.position + pion.velocity;
        setRow3(pion.matrix, pion.position);
        if (pion.angleY != 0) {
            pion.matrix = rotationY(pion.angleY) * pion.matrix;
        }
        if (pion.angleX != 0) {
            pion.matrix = rotationX(pion.angleX) * pion.matrix;
        }
        if (pion.angleZ != 0) {
            pion.matrix = rotationZ(pion.angleZ) * pion.matrix;
        }
    }
}

int Pickups::takeBonus(int piece, int lane)
{
    if (piece == -1 || lane == -1) {
        return bonus_type::none;
    }
    for (auto& bonus : bonuses_) {
        if (bonus.type != bonus_type::none && bonus.piece == piece && bonus.lane == lane) {
            const int type = bonus.type;
            bonus.type = bonus_type::none;
            bonus.piece = -1;
            bonus.lane = -1;
            return type;
        }
    }
    return bonus_type::none;
}

bool Pickups::updatePions(const PionHitter& car, Lcg& random, Vec3& bumpAt)
{
    if (car.piece == -1 || car.lane == -1) {
        return false;
    }
    for (auto& pion : pions_) {
        if (!pion.active) {
            continue;
        }
        if (pion.hit) {
            // Gravity _DAT_00446a85 = -0.125, drag 0.95, tumbling.
            pion.velocity.y += -0.125F;
            pion.velocity.x = times095(pion.velocity.x);
            pion.velocity.z = times095(pion.velocity.z);
            pion.angleY = (pion.angleY + (random.next() & 0xF)) % 0x400;
            pion.angleX = (pion.angleX + (random.next() & 0xF)) % 0x400;
            pion.angleZ = (pion.angleZ + (random.next() & 0xF)) % 0x400;
        }
        if (pion.position.y <= 0.0F) {
            // Retired: hidden, and the record reset to the template at 0x459188.
            pion.active = false;
            pion.hit = false;
            pion.piece = -1;
            pion.lane = -1;
        }
        if (pion.piece == car.piece && pion.lane == car.lane) {
            pion.velocity.x = car.velocity.x;
            pion.velocity.z = car.velocity.z;
            // _DAT_00446a7d = 3.0.
            pion.velocity.y = static_cast<float>(
                (std::abs(pion.velocity.z) + std::abs(pion.velocity.x)) / 3.0);
            const bool first = !pion.hit;
            pion.hit = true;
            if (first) {
                bumpAt = pion.position;
            }
            return first;
        }
    }
    return false;
}

} // namespace opena2::race
