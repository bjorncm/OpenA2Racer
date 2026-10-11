#pragma once

// The other cars of A2SPEL.DAT: the five computer racers, the police and the
// civilian traffic (opponent.cpp, driver.cpp, setracer.cpp, active.cpp). Their
// set-up (FUN_00411296), the lane table of the road, the drivers with their
// lane tactics and police chase, the per-frame movement and collisions of all
// cars with the player (init_player_opponent, 0x407193), the ranking and the
// place signs.
//
// Slot 0 is the player, 1..5 the racers, then the police, then the
// civilians, as in the original's opponent table. One call of step() is one
// frame of the original.

#include "opena2/race/collision.hpp"
#include "opena2/race/player_car.hpp"
#include "opena2/race/race_math.hpp"
#include "opena2/race/track.hpp"

#include <array>
#include <span>
#include <vector>

namespace opena2::race {

// The driver kind of a car (+0xe4) and the type of its driver (+0x10).
namespace driver_kind {
constexpr int none = 0;
constexpr int civilian = 2;
constexpr int racer = 3;
constexpr int police = 4;
} // namespace driver_kind

// The car type of the police (create_opponent reads POLICESPEED for it).
constexpr int policeCarType = 12;

struct TrafficSettings {
    int police = 0;
    int civilians = 0;
    // [RACE] RACERSPEED and POLICESPEED.
    float racerSpeed = 0.0F;
    float policeSpeed = 0.0F;
    // [TWEAK] RACERRANGE, RACERHIGHSPEED, RACERLOWSPEED.
    int racerRange = 0;
    float racerHighSpeed = 0.0F;
    float racerLowSpeed = 0.0F;
};

// A computer car's racer record.
struct TrafficCar {
    // Record +0: the car type (the table at 0x457978).
    int carType = 0;
    int kind = driver_kind::none;
    // +0xe0.
    int driver = -1;
    // +0xe8.
    bool active = true;
    CarBody body;
    // +0x14 and +0x4c.
    float acceleration = 0.2F;
    float topSpeed = 0.0F;
    // +0xcc: the lane the car steers to.
    int targetLane = 0;
    // +0xd4 and +0xd0: the piece it steers to and the previous one.
    int waypoint = 0;
    int previousWaypoint = 0;
    // +0xfc: the heading change of the next movement pass.
    int steer = 0;
    // The object callback (0x40654b): the road attitude and the matrix. A
    // racer that stopped after the finish gets period -1, and its callback
    // no longer runs.
    bool callbacks = true;
    int pitch = 0;
    int roll = 0;
    Mat4 matrix = Mat4::identity();
};

// A driver record (DAT_0047513c, 0x1c bytes).
struct Driver {
    int car = 0;
    // +4 and +8: the lane and speed the driver prefers.
    int lane = 0;
    float speed = 0.0F;
    bool assigned = false;
    int type = driver_kind::none;
    // +0x14: racer - 1 for a racer, the police index for the police.
    int target = -1;
};

// A racer table entry (DAT_00475140, 12 bytes per computer racer).
struct RacerEntry {
    // 4 once the racer is past its finish line.
    int state = 0;
    // The police car that stopped it, -1 when none.
    int stoppedBy = -1;
    // The stop countdown, or the speed after the finish.
    int counter = 0;
};

namespace police_state {
constexpr int stakeOut = 0;
constexpr int chase = 1;
constexpr int cooldown = 2;
} // namespace police_state

// A police table entry (DAT_00475144, 0x18 bytes).
struct PoliceEntry {
    int state = police_state::stakeOut;
    // The racer slot it chases, -1 when none.
    int target = -1;
    int fastEnough = 0;
    int cooldown = 0;
    // The driver's speed and lane when the chase began.
    int speed = 0;
    int lane = 0;
};

// An engine loop of a car near the player (the slot table at 0x4751c4).
struct EngineSlot {
    // The car's slot, -1 for a free slot.
    int car = -1;
    // The sample: motor2, truck01, truck02, truck06 or sirene.
    int sample = 0;
    // The rounded Manhattan distance to the player, 0..500.
    int distance = 0;
};

// What a frame asks of the rest of the game.
struct TrafficEvents {
    // A police car caught the player: set_state(4).
    bool caught = false;
    // set_racer_finish_time for these racers (1..5).
    std::vector<int> finished;
    // bump2.wav from the bounces.
    std::vector<Crash> crashes;
    // bump3.wav: a computer car hit a road wall.
    std::vector<Vec3> wallBumps;
    // The player's own wall and track-end bumps.
    CarEvents player;
};

class Traffic {
public:
    // FUN_00411296 after the player is placed: create_standard_opponents,
    // the racers on the grid behind `startPiece` at `racerPlaces` (RACER_1..5),
    // the police (FUN_00411473) and the civilians (opponent_road_update from
    // piece 30). `boxes` and the masses come from the car type meshes.
    void setUp(std::span<const RoadPiece> pieces, bool forward, int startPiece,
               const PlayerCar& player, const std::array<int, 5>& racerPlaces,
               std::span<const CarBox> boxes, const TrafficSettings& settings, Lcg& random);

    // init_player_opponent (0x407193), after the player's controls: the lane
    // table, the active lists (10 and 12 pieces), update_active_opponents,
    // then the movement and collision passes of every active car with the
    // player. `clockRunning` is DAT_00458c50, `raceState` DAT_00458c64.
    void step(std::span<const RoadPiece> pieces, PlayerCar& player, bool clockRunning,
              int raceState, Lcg& random, TrafficEvents& events);
    // The car object callbacks (0x40654b): road height, attitude and matrix.
    void runCallbacks(std::span<const RoadPiece> pieces);

    // rank_racers (0x411bb0): the places of racers 0..5.
    std::array<int, 6> places(std::span<const RoadPiece> pieces) const;
    // The place signs (types 0x26..0x2b, callbacks 0x408481..): the matrix of
    // the sign of place 1..6 over the racer that has it in `places` (0 = the
    // player), false when none has it.
    bool placeSign(const std::array<int, 6>& places, int place, Mat4& matrix) const;

    // Slot 0 is a placeholder for the player.
    std::span<const TrafficCar> cars() const noexcept { return cars_; }
    std::span<const Driver> drivers() const noexcept { return drivers_; }
    std::span<const RacerEntry> racers() const noexcept { return racers_; }
    std::span<const PoliceEntry> police() const noexcept { return police_; }
    const std::array<EngineSlot, 6>& engineSlots() const noexcept { return engineSlots_; }
    // weg_get_lane_entry: the car on `piece` in `lane`, -1 when none.
    int laneEntry(int piece, int lane) const noexcept;

    // The pion update hitter for car `slot` (FUN_00413e88 runs update_pions
    // for every racer and police car).
    Vec3 velocity(int slot) const noexcept;

private:
    CarBody& body(int slot) noexcept;
    const CarBody& body(int slot) const noexcept;

    void createCar(int carType, int kind, std::span<const CarBox> boxes, float topSpeed);
    void assignDriver(int slot);
    void roadMove(std::span<const RoadPiece> pieces, int slot, bool forward, int piece, int lane,
                  float along);
    void placeCivilians(std::span<const RoadPiece> pieces, int first, int margin, bool forward,
                        Lcg& random);
    void setWaypoint(std::span<const RoadPiece> pieces, int slot, int ahead);

    void setLaneEntry(int slot, int piece, int lane) noexcept;
    void clearLaneEntry(int piece, int lane) noexcept;
    void buildActiveLists();
    void updateActive(std::span<const RoadPiece> pieces, bool clockRunning, int raceState,
                      Lcg& random, TrafficEvents& events);
    void objectStep(std::span<const RoadPiece> pieces, int slot);
    void updateEngineSound(int slot);
    bool turnBlocked(int slot);

    // drive_opponent (0x40ec6c) and its bodies.
    void drive(int driver, int raceState, Lcg& random, TrafficEvents& events);
    void driveCivilian(int driver);
    void driveRacer(int driver);
    void drivePolice(int driver, int raceState, Lcg& random, TrafficEvents& events);
    struct LaneView;
    LaneView viewLanes(int slot) const;
    void chooseLane(int slot, const Driver& driver, const LaneView& view);

    std::vector<TrafficCar> cars_;
    std::vector<Driver> drivers_;
    std::vector<RacerEntry> racers_;
    std::vector<PoliceEntry> police_;
    // One entry per piece and lane: the car's slot or -1.
    std::vector<std::array<int, 11>> lanes_;
    std::vector<int> movers_;
    std::vector<int> partners_;
    std::array<EngineSlot, 6> engineSlots_{};
    TrafficSettings settings_;
    int pieceCount_ = 0;
    bool forward_ = true;
    PlayerCar* player_ = nullptr;
    CarBody playerBody_;
    // The player's lane table entry (piece, lane) of the last frame.
    std::array<int, 2> playerEntry_{-1, -1};
};

} // namespace opena2::race
