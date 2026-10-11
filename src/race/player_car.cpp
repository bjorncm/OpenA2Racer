#include "opena2/race/player_car.hpp"

#include "opena2/race/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace opena2::race {
namespace {

// FISTP in the default rounding mode.
int roundToInt(double value) noexcept
{
    return static_cast<int>(std::nearbyint(value));
}

// The steer table at 0x4577f0 (14 .. -14) and the gain table at 0x457864,
// indexed by |round(speed)|. Entry 16 is the next value in the file.
constexpr std::array<float, 17> steerGain{1.01F, 1.1F, 1.2F, 1.3F, 1.3F, 1.3F, 1.3F, 1.3F, 1.4F,
                                          1.5F, 1.5F, 1.6F, 1.8F, 2.0F, 2.0F, 2.0F, 0.75F};

float steerValue(int index) noexcept
{
    return static_cast<float>(14 - std::clamp(index, 0, 28));
}

// The lane bounds at 0x4590e0: 11 lanes across the road.
constexpr std::array<float, 12> laneBounds{-120.0F, -94.5F, -72.5F, -45.0F, -15.0F, 0.0F,
                                           0.0F,    15.0F,  45.0F,  72.5F,  94.5F, 120.0F};

// The lane offsets at 0x4578d4 used to place a car.
constexpr std::array<float, 11> laneOffsets{108.0F, 85.0F, 60.0F, 30.0F, 0.0F,  0.0F,
                                            0.0F,   -30.0F, -60.0F, -85.0F, -108.0F};

// _DAT_004578a4: the player's bounce decays each frame.
constexpr float bounceDecay = 0.75F;

// FUN_00425e78 through FUN_0041dcc0: the side of (x, z) against the line
// from `from` to `to`, all rounded to integers.
int sideOf(const Vec3& from, const Vec3& to, float x, float z) noexcept
{
    const int fx = roundToInt(from.x);
    const int fz = roundToInt(from.z);
    const int dx = roundToInt(to.x) - fx;
    const int dz = roundToInt(to.z) - fz;
    const auto value = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::int64_t>(roundToInt(x) - fx) * dz) -
        static_cast<std::uint32_t>(static_cast<std::int64_t>(dx) * (roundToInt(z) - fz)));
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

// FUN_0041dd0c: the side of (x, z) against piece `i`'s cross line, from the
// right vertex (+4) to the left vertex (+8).
int crossSide(const RoadPiece& piece, float x, float z) noexcept
{
    return sideOf(piece.right.position, piece.left.position, x, z);
}

} // namespace

const std::vector<SpeedRecord>& speedTable()
{
    static const std::vector<SpeedRecord> table = [] {
        std::vector<SpeedRecord> records;
        // FUN_00402a06(band, speed from, speed to, sound from, sound to, count).
        const auto add = [&records](int band, float speedFrom, float speedTo, float soundFrom,
                                    float soundTo, float count) {
            const float speedStep = (speedTo - speedFrom) / count;
            const float soundStep = (soundTo - soundFrom) / count;
            float speed = speedFrom;
            float sound = soundFrom;
            for (float i = 0.0F; i < count; i += 1.0F) {
                records.push_back({roundToInt(sound), speed, band});
                sound += soundStep;
                speed += speedStep;
            }
        };
        add(0, -2.5F, 0.0F, 50.0F, 0.0F, 7.0F);
        add(1, 0.0F, 2.5F, 0.0F, 55.0F, 7.0F);
        add(2, 2.5F, 4.0F, 35.0F, 60.0F, 12.0F);
        add(3, 4.0F, 6.0F, 40.0F, 63.0F, 15.0F);
        add(4, 6.0F, 8.0F, 45.0F, 65.0F, 30.0F);
        add(5, 8.0F, 15.0F, 45.0F, 60.0F, 80.0F);
        return records;
    }();
    return table;
}

int speedIndexFor(float speed)
{
    const auto& table = speedTable();
    int index = 0;
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].speed <= speed) {
            index = static_cast<int>(i);
        }
    }
    return index;
}

int laneAt(const RoadPiece& piece, float x, float z) noexcept
{
    const int h = piece.heading & angleMask;
    const Vec3& centre = piece.centre.position;
    const float across = (z - centre.z) * angleCos(h) + -angleSin(h) * (x - centre.x);
    for (std::size_t lane = 0; lane + 1 < laneBounds.size(); ++lane) {
        if (laneBounds[lane] <= across && across < laneBounds[lane + 1]) {
            return static_cast<int>(lane);
        }
    }
    return -1;
}

int findPiece(std::span<const RoadPiece> pieces, int start, float x, float z) noexcept
{
    // Piece i needs piece i + 1 for its far line.
    const int count = static_cast<int>(pieces.size()) - 1;
    const auto between = [&](int i) {
        return crossSide(pieces[static_cast<std::size_t>(i)], x, z) !=
               crossSide(pieces[static_cast<std::size_t>(i + 1)], x, z);
    };
    int down = std::clamp(start, 0, std::max(count - 1, 0));
    int up = down + 1;
    while (down > 0 || up < count) {
        if (down > 0) {
            if (between(down)) {
                return down;
            }
            --down;
        }
        if (up < count) {
            if (between(up)) {
                return up;
            }
            ++up;
        }
    }
    return -1;
}

float laneOffset(int lane) noexcept
{
    return laneOffsets[static_cast<std::size_t>(std::clamp(lane, 0, 10))];
}

Vec3 lanePosition(const RoadPiece& piece, int lane, float ahead) noexcept
{
    const float offset = laneOffset(lane);
    return {piece.side.x * offset + piece.centre.position.x - piece.side.z * ahead, 0.0F,
            ahead * piece.side.x + piece.side.z * offset + piece.centre.position.z};
}

void PlayerCar::place(std::span<const RoadPiece> pieces, bool forward, int piece, int lane,
                      float along)
{
    body_.forward = forward;
    body_.piece = std::clamp(piece, 0, static_cast<int>(pieces.size()) - 1);
    lastPiece_ = body_.piece;
    body_.lane = lane;
    const RoadPiece& road = pieces[static_cast<std::size_t>(body_.piece)];
    // Constant 40.0 at 0x44199d.
    body_.position = lanePosition(road, lane, along + 40.0F);
    body_.heading = road.heading + (forward ? -0x100 : 0x100);
    body_.updateCorners();
}

void PlayerCar::placeOnGrid(std::span<const RoadPiece> pieces, bool forward, int startPiece,
                            int place)
{
    const int laneBase = forward ? 2 : 8;
    const float lateral = forward ? 20.0F : -20.0F;
    const int step = forward ? 1 : -1;
    const int row = startPiece - ((place - 1) / 2 + 1) * step;
    const int lane = laneBase + (place % 2) * step;
    const float sign = place % 2 == 1 ? 1.0F : -1.0F;
    this->place(pieces, forward, row, lane, sign * lateral);
}

void PlayerCar::setBox(std::span<const Vec3> vertices)
{
    body_.box = CarBox::fromVertices(vertices);
    body_.updateCorners();
}

// FUN_0040311c: 1 left (toward 0), 2 right (toward 28), 3 centre.
void PlayerCar::steer(int direction)
{
    if (direction == 3) {
        if (steerIndex_ > 14) {
            steerIndex_ = std::max(roundToInt(static_cast<double>(steerIndex_) - ramp_), 14);
        }
        if (steerIndex_ < 14) {
            steerIndex_ = std::min(roundToInt(static_cast<double>(steerIndex_) + ramp_), 14);
        }
        ramp_ = 1.0F;
    } else {
        if (direction == 1 && steerIndex_ > 0) {
            steerIndex_ = std::max(roundToInt(static_cast<double>(steerIndex_) - ramp_), 0);
        } else if (direction == 2 && steerIndex_ < 28) {
            steerIndex_ = roundToInt(static_cast<double>(steerIndex_) + ramp_);
            if (steerIndex_ > 28) {
                steerIndex_ = 27;
            }
        }
        // _DAT_00441c13 = 0.5, capped at 6.0.
        ramp_ = std::min(ramp_ + 0.5F, 6.0F);
    }
    steerUsed_ = steerIndex_;
}

// FUN_00403273: 1 gas (or reverse gas), 2 brake, 3 coast.
void PlayerCar::stepSpeed(int code, CarEvents& events)
{
    const int last = static_cast<int>(speedTable().size()) - 1;
    events.speedStep = true;
    if (throttle_ != 0) {
        if (code == 1) {
            events.gas = true;
            if (throttle_ == 1 && speedIndex_ < last) {
                ++speedIndex_;
            } else if (throttle_ == 2 && speedIndex_ > 0) {
                --speedIndex_;
            }
        } else {
            const int amount = code == 2 ? 4 : 1;
            if (throttle_ == 2) {
                if (speedIndex_ < idleSpeedIndex) {
                    speedIndex_ = std::min(speedIndex_ + amount, idleSpeedIndex);
                }
            } else if (speedIndex_ > idleSpeedIndex) {
                speedIndex_ = std::max(speedIndex_ - amount, idleSpeedIndex);
            }
        }
    }
    const SpeedRecord& record = speedTable()[static_cast<std::size_t>(speedIndex_)];
    if (record.band != band_) {
        events.bandChanged = true;
        events.bandUp = record.band > band_;
    }
    soundIndex_ = record.sound;
    body_.speed = record.speed;
    band_ = record.band;
}

void PlayerCar::control(const CarInput& input, bool hasControl, CarEvents& events)
{
    const int headingBefore = body_.heading;
    turn_ = 0;
    if (!hasControl) {
        return;
    }
    if (input.wheelSteer >= 0) {
        // Above 28 becomes 27, as the body has it.
        steerIndex_ = input.wheelSteer > 28 ? 27 : std::max(input.wheelSteer, 0);
        steerUsed_ = steerIndex_;
    } else if (input.left) {
        steer(1);
    } else if (input.right) {
        steer(2);
    } else {
        steer(3);
    }

    // _DAT_00441c17 = 0.01.
    if (std::abs(body_.speed) >= 0.01F && throttle_ != 0) {
        const auto gainIndex = static_cast<std::size_t>(
            std::min(std::abs(roundToInt(body_.speed)), static_cast<int>(steerGain.size()) - 1));
        const double turn = static_cast<double>(steerValue(steerUsed_)) / steerGain[gainIndex];
        body_.heading = roundToInt(throttle_ == 1 ? body_.heading + turn : body_.heading - turn);
    }
    turn_ = body_.heading - headingBefore;

    if (input.gas) {
        throttle_ = 1;
        stepSpeed(1, events);
    } else if (input.brake) {
        if (speedIndex_ == idleSpeedIndex) {
            throttle_ = 2;
        }
        if (throttle_ == 1) {
            stepSpeed(2, events);
        } else if (throttle_ == 2) {
            stepSpeed(1, events);
        }
    } else {
        stepSpeed(3, events);
    }

    if (lastPiece_ != body_.piece) {
        events.enteredPiece = true;
        const bool back = body_.forward ? body_.piece < lastPiece_ : lastPiece_ < body_.piece;
        if (back && !backwardsPlayed_) {
            backwardsPlayed_ = true;
            events.backwards = true;
        }
        lastPiece_ = body_.piece;
    }
}

void PlayerCar::move(std::span<const RoadPiece> pieces, CarEvents& events)
{
    prepareMove(pieces, events);
    applyStep();
    endMove();
}

void PlayerCar::prepareMove(std::span<const RoadPiece> pieces, CarEvents& events)
{
    body_.updateCorners();
    body_.startStep();
    if (!pieces.empty()) {
        hitRoadWalls(pieces, body_, events.bump);
        hitTrackEnds(pieces, body_, events.bump);
    }
}

void PlayerCar::applyStep() noexcept
{
    body_.position.x = body_.step.x + body_.position.x;
    body_.position.z = body_.step.z + body_.position.z;
    body_.updateCorners();
}

void PlayerCar::endMove()
{
    body_.bounce = body_.bounce * bounceDecay;
    speedIndex_ = speedIndexFor(body_.speed);
}

void PlayerCar::followRoad(std::span<const RoadPiece> pieces)
{
    if (pieces.size() < 2) {
        return;
    }
    const int found = findPiece(pieces, body_.piece, body_.position.x, body_.position.z);
    if (found >= 0) {
        body_.piece = found;
    }
    const RoadPiece& road = pieces[static_cast<std::size_t>(body_.piece)];
    body_.lane = laneAt(road, body_.position.x, body_.position.z);

    // _DAT_00457714 = 3.0; DAT_0045b264 (height offset) stays 0.
    const float heightDivisor = std::max(3.0F - std::abs(body_.speed), 1.0F);
    body_.position.y = (roadHeightAt(road, body_.position.x, body_.position.z) + 0.0F - body_.position.y) /
                      heightDivisor +
                  body_.position.y;

    // The slope 8 units ahead and behind, and 4 units to either side, both on
    // the plane of the current piece; 128 times the normalised rise.
    const auto slope = [&](float ox, float oz) {
        const float y0 = roadHeightAt(road, body_.position.x - ox, body_.position.z - oz);
        const float y1 = roadHeightAt(road, body_.position.x + ox, body_.position.z + oz);
        return normalise(Vec3{2.0F * ox, y1 - y0, 2.0F * oz}).y * 128.0F;
    };
    const int forwardAngle = (body_.heading + 0x100) & angleMask;
    const float pitchDivisor = std::max(3.0F - body_.speed, 1.0F);
    pitchSmooth_ = static_cast<float>(
        (static_cast<double>(slope(angleCos(forwardAngle) * 8.0F, angleSin(forwardAngle) * 8.0F)) -
         pitchSmooth_) /
            pitchDivisor +
        pitchSmooth_);
    pitch_ = roundToInt(pitchSmooth_);
    const int sideAngle = body_.heading & angleMask;
    // _DAT_0045771c = 3.0.
    rollSmooth_ =
        (slope(angleCos(sideAngle) * 4.0F, angleSin(sideAngle) * 4.0F) - rollSmooth_) / 3.0F +
        rollSmooth_;
    roll_ = roundToInt(rollSmooth_);
}

Mat4 PlayerCar::worldMatrix() const noexcept
{
    // Each rotation is applied on the left of the translation matrix.
    return rotationZ(roll_) * (rotationX(-pitch_) * (rotationY(-body_.heading) *
                                                       Mat4::translation(body_.position)));
}

int cameraPitchForHeight(float height) noexcept
{
    // atan_lookup type 4: index round(height + 0.5), clamped to 0..999.
    const int index = std::clamp(roundToInt(static_cast<double>(height) + 0.5), 0, 999);
    // FUN_0042211d: 0.5 - (256 + 256 * (C - atan2(i, -61)) / C), C = 1.570798
    // (0x44d655).
    constexpr double c = 1.570798;
    const double angle = c - std::atan2(static_cast<double>(index), -61.0);
    return roundToInt(-(angle * 256.0 / c + 256.0) + 0.5);
}

void RaceCamera::setPosition(int position) noexcept
{
    switch (position) {
    case 1:
        distance_ = -31.0F;
        height_ = 15.0F;
        break;
    case 2:
        distance_ = -4.0F;
        height_ = 7.0F;
        break;
    case 3:
        distance_ = -31.0F;
        height_ = 32.0F;
        break;
    case 4:
        distance_ = -31.0F;
        height_ = 511.0F;
        break;
    default:
        return;
    }
    position_ = position;
}

void RaceCamera::startFlyIn(const PlayerCar& car) noexcept
{
    setPosition(1);
    height_ = 350.0F;
    pitchSmooth_ = static_cast<float>(cameraPitchForHeight(350.0F));
    headingSmooth_ = static_cast<float>(car.heading());
    smoothed_ = {car.position().x, car.position().y + height_, car.position().z};
}

void RaceCamera::update(const PlayerCar& car) noexcept
{
    const Vec3& p = car.position();
    const int angle = (car.heading() + 0x100) & angleMask;
    const float c = angleCos(angle);
    const float s = angleSin(angle);
    if (position_ == 2) {
        // camera_update_2: fixed to the car; the z offset uses the cosine,
        // as the original does.
        view_.position = {p.x + c * distance_, p.y + height_, c * distance_ + p.z};
        view_.pitch = car.pitch();
        view_.heading = car.heading();
        view_.roll = -car.roll();
        smoothed_ = view_.position;
        return;
    }
    // camera_update_1 and _4: 0.25 of the way to the chase point; heading and
    // pitch a third of the way (_DAT_00457714, _DAT_00457718 = 3.0).
    const Vec3 target{p.x + c * distance_, p.y + height_, p.z + s * distance_};
    smoothed_.x = (target.x - smoothed_.x) * 0.25F + smoothed_.x;
    smoothed_.y = (target.y - smoothed_.y) * 0.25F + smoothed_.y;
    smoothed_.z = (target.z - smoothed_.z) * 0.25F + smoothed_.z;
    headingSmooth_ = (static_cast<float>(car.heading()) - headingSmooth_) / 3.0F + headingSmooth_;
    pitchSmooth_ = static_cast<float>(
        (static_cast<double>(cameraPitchForHeight(height_)) - pitchSmooth_) / 3.0 + pitchSmooth_);
    view_.position = smoothed_;
    view_.pitch = roundToInt(pitchSmooth_);
    view_.heading = roundToInt(headingSmooth_);
    view_.roll = 0;
}

} // namespace opena2::race
