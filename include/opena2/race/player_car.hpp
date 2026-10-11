#pragma once

// The player's car of A2SPEL.DAT: the speed table and its index steps, the
// steer ramp, movement, the road walls, the road following (piece, height,
// pitch and roll) and the cameras.
//
// One call of each step is one frame of the original.

#include "opena2/race/collision.hpp"
#include "opena2/race/race_math.hpp"
#include "opena2/race/track.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace opena2::race {

// One record of the speed table (FUN_00402a06).
struct SpeedRecord {
    // Index into the engine sound tables.
    int sound = 0;
    // Speed in world units per frame.
    float speed = 0.0F;
    int band = 0;
};

// The 151 records load_control_keys (0x402ac7) builds: six bands from
// reverse (-2.5) to top speed (15.0).
const std::vector<SpeedRecord>& speedTable();
// DAT_004729c0: the first record of band 1, speed 0.
constexpr int idleSpeedIndex = 7;
// FUN_004029ab: the last record whose speed is at most `speed`.
int speedIndexFor(float speed);

// The player's controls for one frame, already read from the device.
struct CarInput {
    bool left = false;
    bool right = false;
    bool gas = false;
    bool brake = false;
    // Wheel (BEDIENING 2): the steer index 0..28 from the wheel position;
    // -1 for the keyboard and the joypad, which use the steer ramp.
    int wheelSteer = -1;
};

// The lane offsets at 0x4578d4, lane 0..10.
float laneOffset(int lane) noexcept;
// The point of `lane` on `piece`, `ahead` units along the road, as
// set_racer_place, set_bonus_object and the pion set-up compute it; y is 0.
Vec3 lanePosition(const RoadPiece& piece, int lane, float ahead) noexcept;

// FUN_00411c93: the lane 0..10 of a point across the piece, or -1.
int laneAt(const RoadPiece& piece, float x, float z) noexcept;

// FUN_0041de40: searching outward from `start` (down, then up, in turn), the
// first piece whose cross line and the next piece's cross line lie on
// different sides of (x, z); -1 when there is none.
int findPiece(std::span<const RoadPiece> pieces, int start, float x, float z) noexcept;

// What a car step asks of the rest of the game.
struct CarEvents {
    // The player entered another piece: fuel_tick(1).
    bool enteredPiece = false;
    // Sound 0x2f, played once, the first time the car goes back a piece.
    bool backwards = false;
    // FUN_004105b4: a wall was hit above speed 4.
    bool bump = false;
    // engine_pitch: the speed band changed, up or down.
    bool bandChanged = false;
    bool bandUp = false;
    // FUN_00403273 ran: the engine voices follow the new sound index, the
    // high voice with the gas boost when `gas`.
    bool speedStep = false;
    bool gas = false;
};

class PlayerCar {
public:
    // FUN_00401f79 from set_racer_place (0x411928): the car on `piece` in
    // `lane`, `along` units along the road (plus 40), facing the driving
    // direction. `forward` is the stage's direction flag (racer +0xd8).
    void place(std::span<const RoadPiece> pieces, bool forward, int piece, int lane, float along);
    // set_racer_place for the player: the grid slot of starting place
    // `place` behind `startPiece`.
    void placeOnGrid(std::span<const RoadPiece> pieces, bool forward, int startPiece, int place);
    // FUN_0040a17d: the local box from the mesh's x/z extent.
    void setBox(std::span<const Vec3> vertices);
    // Record +0x30, from the car type table.
    void setMass(float mass) noexcept { body_.mass = mass; }

    // FUN_004034bb: steering, turning and the speed table step. Nothing
    // happens without control.
    void control(const CarInput& input, bool hasControl, CarEvents& events);
    // init_player_opponent (0x407193) for the player alone: move along the
    // heading plus the bounce, held by the road walls. The same as
    // prepareMove, applyStep and endMove in turn.
    void move(std::span<const RoadPiece> pieces, CarEvents& events);
    // The movement pass for the player: the step from the heading, speed and
    // bounce, held by the road walls and the track ends.
    void prepareMove(std::span<const RoadPiece> pieces, CarEvents& events);
    // The collision pass: the position moves by the (shortened) step.
    void applyStep() noexcept;
    // The end of the step: the bounce decays and the speed index follows the
    // speed the walls and collisions left.
    void endMove();
    // The road part of the camera updates (camera_update_1/2/4): the piece
    // under the car, the height and the pitch and roll of the road.
    void followRoad(std::span<const RoadPiece> pieces);
    // clear_car_lateral (0x41266a).
    void stop() noexcept { body_.speed = 0.0F; }

    // The player's racer record (slot 0).
    CarBody& body() noexcept { return body_; }
    const CarBody& body() const noexcept { return body_; }
    // The heading change of the last control step (record +0xfc).
    int turn() const noexcept { return turn_; }

    const Vec3& position() const noexcept { return body_.position; }
    // DAT_0045b250: 1024ths, not wrapped.
    int heading() const noexcept { return body_.heading; }
    float speed() const noexcept { return body_.speed; }
    int speedIndex() const noexcept { return speedIndex_; }
    // DAT_0045b2ac: the engine sound index of the speed record.
    int soundIndex() const noexcept { return soundIndex_; }
    int band() const noexcept { return band_; }
    // DAT_0045773c.
    int piece() const noexcept { return body_.piece; }
    int lane() const noexcept { return body_.lane; }
    bool forward() const noexcept { return body_.forward; }
    int steerIndex() const noexcept { return steerUsed_; }
    // DAT_00457748: the steer index the ramp moves to (the dashboard wheel).
    int steerTarget() const noexcept { return steerIndex_; }
    // DAT_0045b24c and DAT_0045b254.
    int pitch() const noexcept { return pitch_; }
    int roll() const noexcept { return roll_; }
    // World corners (racer +0x58..).
    const std::array<Vec3, 4>& corners() const noexcept { return body_.corners; }

    // The player car body callback (0x402468): rotZ(roll) * rotX(-pitch) *
    // rotY(-heading), then the position.
    Mat4 worldMatrix() const noexcept;

private:
    void stepSpeed(int code, CarEvents& events);
    void steer(int direction);

    CarBody body_;
    int turn_ = 0;
    int speedIndex_ = idleSpeedIndex;
    int soundIndex_ = 0;
    int band_ = 1;
    // DAT_00457754: 0 none yet, 1 forward, 2 reverse.
    int throttle_ = 0;
    // DAT_00457748 (ramp target), DAT_00457750 (used), DAT_0045b2a8 (ramp).
    int steerIndex_ = 14;
    int steerUsed_ = 14;
    float ramp_ = 0.0F;

    int lastPiece_ = 0;
    bool backwardsPlayed_ = false;

    float pitchSmooth_ = 0.0F;
    float rollSmooth_ = 0.0F;
    int pitch_ = 0;
    int roll_ = 0;
};

// The table built by FUN_0042211d(4, 0, 1000, 1, -61.0, 256.0) and read by
// atan_lookup: the camera pitch for a camera height.
int cameraPitchForHeight(float height) noexcept;

struct CameraView {
    Vec3 position;
    int pitch = 0;
    int heading = 0;
    int roll = 0;
};

class RaceCamera {
public:
    // set_camera_position (0x402d0c), positions 1..4.
    void setPosition(int position) noexcept;
    int position() const noexcept { return position_; }
    // DAT_00457724: the fly-in counter is the camera height.
    void setHeight(float height) noexcept { height_ = height; }
    float height() const noexcept { return height_; }
    // race_countdown_start (0x4116cb): the camera jumps above the car, 350
    // units up.
    void startFlyIn(const PlayerCar& car) noexcept;
    // update_camera (0x402e48): chase (1, 4) or cockpit (2).
    void update(const PlayerCar& car) noexcept;
    const CameraView& view() const noexcept { return view_; }
    // Object flag +0x35 of the player car: hidden in the cockpit.
    bool carVisible() const noexcept { return position_ != 2; }

private:
    int position_ = 1;
    float distance_ = -3.0F;
    float height_ = 7.0F;
    Vec3 smoothed_;
    float headingSmooth_ = 0.0F;
    float pitchSmooth_ = 0.0F;
    CameraView view_;
};

} // namespace opena2::race
