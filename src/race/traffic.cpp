#include "opena2/race/traffic.hpp"

#include "opena2/race/scene.hpp"
#include "opena2/race/stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace opena2::race {
namespace {

// FISTP in the default rounding mode.
int roundToInt(double value) noexcept
{
    return static_cast<int>(std::nearbyint(value));
}

// create_standard_opponents (0x4114ba): the racers' car types, racer 1..5.
constexpr std::array<int, 5> racerCarTypes{0x15, 0x12, 0x11, 0x16, 0x14};

// The lane status ranges of FUN_0040d80e (0x444733, 0x44473b): squared
// distances less the two radius values, 240 and 320 units.
constexpr float closeRange = 57600.0F;
constexpr float farRange = 102400.0F;
// _DAT_004456b2, the civilians' second test of the car ahead.
constexpr float overtakeRange = 240.0F;

// The police (0x444af8, 0x444ae8, 0x444af0) and the stop of a caught racer
// (0x44512b, 0x445133).
constexpr float chaseSpeed = 9.0F;
constexpr int policeCooldown = 0x15e;
constexpr int stopFrames = 100;
constexpr float stoppedSpeed = 2.0F;

// FUN_00405b09: steer = |side| * 32.0 * 1.5 (0x4421bd, 0x4421c5).
constexpr double steerGain = 32.0 * 1.5;
// opponent_object_step: the engine sound range (0x4422c7) and the bounce
// decay (_DAT_004578a4).
constexpr float soundRange = 500.0F;
constexpr float bounceDecay = 0.75F;
// init_player_opponent: turns below this speed may not push into a car.
constexpr float turnCheckSpeed = 4.0F;

// The sample of a car's engine loop (FUN_004101f4) by car type.
int engineSample(int carType) noexcept
{
    switch (carType) {
    case 1:
        return 15; // truck01
    case 5:
        return 16; // truck02
    case 11:
        return 19; // truck06
    case policeCarType:
        return 8; // sirene
    default:
        return 0; // motor2
    }
}

// FUN_0040d7ed: the squared distance less both radius values.
float range(const CarBody& a, const CarBody& b) noexcept
{
    const float dx = a.position.x - b.position.x;
    const float dz = a.position.z - b.position.z;
    return dx * dx + dz * dz - (a.box.radiusSquared + b.box.radiusSquared);
}

// The point of `lane` across `piece`, `ahead` units along it, with the
// piece's height (opponent_road_move and opponent_road_update).
Vec3 roadPoint(const RoadPiece& piece, int lane, float ahead) noexcept
{
    const float offset = laneOffset(lane);
    Vec3 p = piece.centre.position + piece.side * offset;
    p.x -= piece.side.z * ahead;
    p.z += piece.side.x * ahead;
    return p;
}

int at(const std::array<int, 11>& lanes, int lane) noexcept
{
    return lane >= 0 && lane < 11 ? lanes[static_cast<std::size_t>(lane)] : -1;
}

} // namespace

struct Traffic::LaneView {
    std::array<int, 11> front{};
    std::array<int, 11> back{};
    // Own lane, own + step, own - step; their status and the car ahead.
    std::array<int, 3> lanes{};
    std::array<int, 3> status{};
    std::array<int, 3> other{-1, -1, -1};
    int count = 0;
    // The edge lanes of the driving direction: 0 and 3, or 10 and 7.
    int edge = 0;
    int inner = 0;
};

CarBody& Traffic::body(int slot) noexcept
{
    if (slot == 0) {
        return player_ != nullptr ? player_->body() : playerBody_;
    }
    return cars_[static_cast<std::size_t>(slot)].body;
}

const CarBody& Traffic::body(int slot) const noexcept
{
    if (slot == 0) {
        return player_ != nullptr ? player_->body() : playerBody_;
    }
    return cars_[static_cast<std::size_t>(slot)].body;
}

int Traffic::laneEntry(int piece, int lane) const noexcept
{
    if (piece < 0 || piece >= static_cast<int>(lanes_.size())) {
        return -1;
    }
    return at(lanes_[static_cast<std::size_t>(piece)], lane);
}

void Traffic::setLaneEntry(int slot, int piece, int lane) noexcept
{
    if (piece >= 0 && piece < static_cast<int>(lanes_.size()) && lane >= 0 && lane < 11) {
        lanes_[static_cast<std::size_t>(piece)][static_cast<std::size_t>(lane)] = slot;
    }
}

void Traffic::clearLaneEntry(int piece, int lane) noexcept
{
    setLaneEntry(-1, piece, lane);
}

Vec3 Traffic::velocity(int slot) const noexcept
{
    const CarBody& b = body(slot);
    return {b.direction.x * b.speed, 0.0F, b.direction.z * b.speed};
}

// create_opponent (0x4056a9). The acceleration test reads the driver kind
// before it is stored, so every car gets 0.2.
void Traffic::createCar(int carType, int kind, std::span<const CarBox> boxes, float topSpeed)
{
    TrafficCar car;
    car.carType = carType;
    car.kind = kind;
    const auto type = static_cast<std::size_t>(carType);
    if (type < boxes.size()) {
        car.body.box = boxes[type];
    }
    car.body.mass = carTypes()[type].mass;
    car.topSpeed = topSpeed;
    car.body.direction = {angleCos(0), 0.0F, angleSin(0)};
    // scan_driver_list: the next free driver.
    car.driver = static_cast<int>(drivers_.size());
    drivers_.push_back({});
    cars_.push_back(car);
}

// assign_driver_to_opponent (0x40d414).
void Traffic::assignDriver(int slot)
{
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    if (car.driver < 0) {
        return;
    }
    Driver& driver = drivers_[static_cast<std::size_t>(car.driver)];
    driver.car = slot;
    if (car.body.forward) {
        driver.lane = car.kind == driver_kind::racer ? 1 : 2;
    } else {
        driver.lane = car.kind == driver_kind::racer ? 9 : 8;
    }
    driver.speed = car.topSpeed;
    driver.assigned = true;
    driver.type = car.kind;
    if (car.kind == driver_kind::civilian) {
        driver.target = -1;
    } else if (car.kind == driver_kind::racer) {
        driver.target = slot - 1;
    } else if (car.kind == driver_kind::police) {
        driver.target = slot - 6;
    }
}

// set_opponent_waypoint (0x405cf4): the piece `ahead` pieces on. Past the
// end of the track the car is put back at the start (piece 3, or the
// fourth-last piece backward).
void Traffic::setWaypoint(std::span<const RoadPiece> pieces, int slot, int ahead)
{
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    if (car.waypoint < 0) {
        car.waypoint = car.previousWaypoint;
        return;
    }
    car.previousWaypoint = car.waypoint;
    int wrap = -1;
    if (car.body.forward) {
        car.waypoint = car.body.piece + ahead;
        if (car.waypoint > pieceCount_ - 1) {
            car.body.piece = 3;
            car.waypoint = 5;
            wrap = 3;
            car.body.heading = pieces[3].heading - 0x100;
        }
    } else {
        car.waypoint = car.body.piece - ahead;
        if (car.waypoint < 0) {
            wrap = pieceCount_ - 4;
            car.body.piece = wrap;
            car.waypoint = pieceCount_ - 6;
            car.body.heading = pieces[static_cast<std::size_t>(wrap)].heading + 0x100;
        }
    }
    if (wrap != -1) {
        // 40 units back from the piece (_DAT_0044226f), in the lane it was in.
        car.body.position = roadPoint(pieces[static_cast<std::size_t>(wrap)], car.body.lane, -40.0F);
        car.body.step = {};
        car.body.bounce = {};
        car.steer = 0;
    }
}

// opponent_road_move (0x40684c).
void Traffic::roadMove(std::span<const RoadPiece> pieces, int slot, bool forward, int piece,
                       int lane, float along)
{
    piece = std::clamp(piece, 0, pieceCount_ - 1);
    lane = std::clamp(lane, 0, 10);
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    car.body.forward = forward;
    car.targetLane = lane;
    car.body.lane = lane;
    // _DAT_004425c4 = 40.0.
    car.body.position = roadPoint(pieces[static_cast<std::size_t>(piece)], lane, along + 40.0F);
    car.body.piece = piece;
    setWaypoint(pieces, slot, 2);
    setLaneEntry(slot, piece, lane);
    car.previousWaypoint = forward ? piece - 1 : (piece + 1) % (pieceCount_ - 1);
    assignDriver(slot);
    car.body.heading = pieces[static_cast<std::size_t>(piece)].heading + (forward ? -0x100 : 0x100);
    car.body.speed = 0.0F;
    car.body.direction = {};
    car.steer = 0;
    car.topSpeed = 0.0F;
}

// opponent_road_update (0x406c45): a quarter of the cars drive against the
// race, each on a free lane of a random piece.
void Traffic::placeCivilians(std::span<const RoadPiece> pieces, int first, int margin,
                             bool forward, Lcg& random)
{
    const int span = std::max(pieceCount_ - 3 * margin, 1);
    for (int slot = first; slot < static_cast<int>(cars_.size()); ++slot) {
        bool heading = forward;
        int piece = 0;
        int lane = 0;
        do {
            heading = random.next() % 4 == 0 ? !forward : forward;
            piece = margin + random.next() % span;
            lane = random.next() % 2 + (heading ? 2 : 7);
        } while (laneEntry(piece, lane) != -1 && piece < pieceCount_);
        piece = std::min(piece, pieceCount_ - 1);
        setLaneEntry(slot, piece, lane);
        TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
        car.body.piece = piece;
        car.waypoint = piece;
        car.targetLane = lane;
        car.body.lane = lane;
        car.body.forward = heading;
        car.body.position = roadPoint(pieces[static_cast<std::size_t>(piece)], lane, 1.0F);
        car.body.speed = 0.0F;
        car.previousWaypoint = heading ? piece - 1 : piece + 1;
        assignDriver(slot);
        car.body.heading = pieces[static_cast<std::size_t>(piece)].heading + (heading ? -0x100 : 0x100);
        car.body.direction = {};
        car.steer = 0;
    }
}

void Traffic::setUp(std::span<const RoadPiece> pieces, bool forward, int startPiece,
                    const PlayerCar& player, const std::array<int, 5>& racerPlaces,
                    std::span<const CarBox> boxes, const TrafficSettings& settings, Lcg& random)
{
    pieceCount_ = static_cast<int>(pieces.size());
    forward_ = forward;
    settings_ = settings;
    player_ = nullptr;
    playerBody_ = player.body();
    lanes_.assign(pieces.size(), {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1});
    cars_.clear();
    drivers_.clear();
    engineSlots_.fill({});
    playerEntry_ = {-1, -1};
    if (pieceCount_ < 2) {
        return;
    }

    // Slot 0 stands for the player (FUN_00401f79).
    TrafficCar playerSlot;
    playerSlot.carType = 19;
    cars_.push_back(playerSlot);

    for (const int type : racerCarTypes) {
        createCar(type, driver_kind::racer, boxes, carTypes()[static_cast<std::size_t>(type)].topSpeed);
    }
    for (int i = 0; i < settings.police; ++i) {
        createCar(policeCarType, driver_kind::police, boxes, settings.policeSpeed);
    }
    // Civilians cycle through car types 0..15 without 4 (the player's BMW)
    // and 12 (the police).
    int type = 0;
    for (int i = 0; i < settings.civilians; ++i) {
        createCar(type, driver_kind::civilian, boxes, carTypes()[static_cast<std::size_t>(type)].topSpeed);
        ++type;
        while (true) {
            if (type == 16) {
                type = 0;
            }
            if (type != 4 && type != policeCarType) {
                break;
            }
            ++type;
        }
    }
    racers_.assign(5, {});
    police_.assign(static_cast<std::size_t>(settings.police), {});

    // FUN_00411401: set_racer_place for racers 1..5, top speed RACERSPEED.
    const int step = forward ? 1 : -1;
    const int laneBase = forward ? 2 : 8;
    const float lateral = forward ? 20.0F : -20.0F;
    for (int racer = 1; racer <= 5; ++racer) {
        const int place = racerPlaces[static_cast<std::size_t>(racer - 1)];
        const int row = startPiece - ((place - 1) / 2 + 1) * step;
        const int lane = laneBase + (place % 2) * step;
        const float sign = place % 2 == 1 ? 1.0F : -1.0F;
        roadMove(pieces, racer, forward, row, lane, sign * lateral);
        drivers_[static_cast<std::size_t>(cars_[static_cast<std::size_t>(racer)].driver)].speed =
            settings.racerSpeed;
    }

    // FUN_00411473 and get_police_record: on the hard shoulder, spread
    // [spacing, 2 * spacing) pieces apart.
    if (settings.police > 0) {
        const int spacing = std::max(pieceCount_ / (settings.police * 2), 1);
        int piece = 0;
        for (int i = 0; i < settings.police; ++i) {
            piece += random.next() % spacing + spacing;
            const int lane = forward ? random.next() % 2 : 10 - random.next() % 2;
            roadMove(pieces, 6 + i, forward, piece, lane, 0.0F);
        }
    }
    // opponent_default_step: the civilians from piece 30.
    placeCivilians(pieces, 6 + settings.police, 30, forward, random);
}

void Traffic::buildActiveLists()
{
    movers_.clear();
    partners_.clear();
    const int fixed = std::min(6, static_cast<int>(cars_.size()));
    for (int slot = 0; slot < fixed; ++slot) {
        movers_.push_back(slot);
        partners_.push_back(slot);
    }
    const int playerPiece = body(0).piece;
    for (int slot = 6; slot < static_cast<int>(cars_.size()); ++slot) {
        if (!cars_[static_cast<std::size_t>(slot)].active) {
            continue;
        }
        const int distance = std::abs(cars_[static_cast<std::size_t>(slot)].body.piece - playerPiece);
        if (distance < 10) {
            movers_.push_back(slot);
        } else if (distance >= 12) {
            continue;
        }
        partners_.push_back(slot);
    }
}

// FUN_004103a5 and stop_slot_sound: up to six cars within 500 units keep
// an engine loop.
void Traffic::updateEngineSound(int slot)
{
    const Vec3& car = body(slot).position;
    const Vec3& player = body(0).position;
    const float distance =
        std::abs(car.z - player.z) + std::abs(car.y - player.y) + std::abs(car.x - player.x);
    auto found = std::find_if(engineSlots_.begin(), engineSlots_.end(),
                              [slot](const EngineSlot& s) { return s.car == slot; });
    if (soundRange <= distance) {
        if (found != engineSlots_.end()) {
            *found = {};
        }
        return;
    }
    const int rounded = roundToInt(distance);
    if (found != engineSlots_.end()) {
        found->distance = rounded;
        return;
    }
    const auto free = std::find_if(engineSlots_.begin(), engineSlots_.end(),
                                   [](const EngineSlot& s) { return s.car == -1; });
    if (free != engineSlots_.end()) {
        *free = {slot, engineSample(cars_[static_cast<std::size_t>(slot)].carType), rounded};
    }
}

// opponent_object_step (0x40610f) with opponent_speed_step and FUN_00405b09.
void Traffic::objectStep(std::span<const RoadPiece> pieces, int slot)
{
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    if (!car.active) {
        return;
    }
    updateEngineSound(slot);

    // Steer at the waypoint piece's point in the target lane.
    const auto waypoint = static_cast<std::size_t>(std::clamp(car.waypoint, 0, pieceCount_ - 1));
    const RoadPiece& road = pieces[waypoint];
    const Vec3 target = road.centre.position + road.side * laneOffset(car.targetLane);
    const Vec3 toward = normalise(target - car.body.position);
    const float side = toward.x * car.body.direction.z - toward.z * car.body.direction.x;
    const int amount = roundToInt(std::abs(static_cast<double>(side)) * steerGain);
    car.steer = side < 0.0F ? amount : -amount;

    if (car.body.speed < car.topSpeed) {
        car.body.speed = car.acceleration + car.body.speed;
        if (car.body.speed > car.topSpeed) {
            car.body.speed = car.topSpeed;
        }
    } else {
        car.body.speed = car.topSpeed;
    }
    car.body.bounce.x = bounceDecay * car.body.bounce.x;
    car.body.bounce.z = bounceDecay * car.body.bounce.z;
}

// update_active_opponents (0x406257).
void Traffic::updateActive(std::span<const RoadPiece> pieces, bool clockRunning, int raceState,
                           Lcg& random, TrafficEvents& events)
{
    for (std::size_t k = 1; k < movers_.size(); ++k) {
        const int slot = movers_[k];
        const TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
        // Before the start only slots above 13 drive.
        if (car.carType != 0x10 && (clockRunning || slot > 13)) {
            drive(car.driver, raceState, random, events);
        }
    }
    for (std::size_t k = 1; k < movers_.size(); ++k) {
        const int slot = movers_[k];
        TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
        if (car.carType == 0x10) {
            continue;
        }
        clearLaneEntry(car.body.piece, car.body.lane);
        objectStep(pieces, slot);
        const int found = findPiece(pieces, car.body.piece, car.body.position.x, car.body.position.z);
        if (found >= 0) {
            car.body.piece = found;
        }
        setWaypoint(pieces, slot, 2);
        car.body.lane = laneAt(pieces[static_cast<std::size_t>(car.body.piece)],
                               car.body.position.x, car.body.position.z);
        setLaneEntry(slot, car.body.piece, car.body.lane);

        if (slot < 1 || slot > 5) {
            continue;
        }
        // The rubber band: a racer ahead of the player by more than
        // RACERRANGE pieces drops to RACERLOWSPEED; one behind gets
        // RACERHIGHSPEED.
        const auto ranks = places(pieces);
        Driver& driver = drivers_[static_cast<std::size_t>(car.driver)];
        if (ranks[static_cast<std::size_t>(slot)] < ranks[0]) {
            if (settings_.racerRange < std::abs(car.body.piece - body(0).piece)) {
                driver.speed = settings_.racerLowSpeed;
            }
        } else if (ranks[0] < ranks[static_cast<std::size_t>(slot)]) {
            driver.speed = settings_.racerHighSpeed;
        }
        // Past its finish, 15 pieces from the end, the racer slows to a stop.
        RacerEntry& racer = racers_[static_cast<std::size_t>(slot - 1)];
        int past = -1;
        if (car.body.forward) {
            if (pieceCount_ - 15 <= car.body.piece) {
                past = car.body.piece - (pieceCount_ - 15);
            }
        } else if (car.body.piece < 16) {
            past = 15 - car.body.piece;
        }
        if (past >= 0) {
            racer.counter = 10 - past;
            events.finished.push_back(slot);
            racer.state = 4;
        }
    }
}

bool Traffic::turnBlocked(int slot)
{
    for (const int other : partners_) {
        if (other == slot || std::abs(body(other).piece - body(slot).piece) >= 2) {
            continue;
        }
        body(other).updateCorners();
        if (collideCars(body(slot), body(other), false) != contact::none) {
            return true;
        }
    }
    return false;
}

void Traffic::step(std::span<const RoadPiece> pieces, PlayerCar& player, bool clockRunning,
                   int raceState, Lcg& random, TrafficEvents& events)
{
    player_ = &player;
    if (cars_.empty() || pieces.size() < 2) {
        player.move(pieces, events.player);
        return;
    }

    // The player's record and its lane entry.
    CarBody& self = player.body();
    clearLaneEntry(playerEntry_[0], playerEntry_[1]);
    const int playerLane = laneAt(pieces[static_cast<std::size_t>(self.piece)], self.position.x,
                                  self.position.z);
    setLaneEntry(0, self.piece, playerLane);
    playerEntry_ = {self.piece, playerLane};

    buildActiveLists();
    updateActive(pieces, clockRunning, raceState, random, events);

    // The movement pass.
    for (const int slot : movers_) {
        CarBody& car = body(slot);
        int turn = player.turn();
        if (slot != 0) {
            turn = cars_[static_cast<std::size_t>(slot)].steer;
            car.heading += turn;
        }
        car.updateCorners();
        if (turn != 0 && (slot == 0 || std::abs(car.piece - self.piece) <= 1)) {
            car.step = {};
            if (car.speed < turnCheckSpeed) {
                // Up to four quarter steps back while the turn touches a car;
                // the fourth test passing puts the whole turn back, as the
                // body has it.
                const int heading = car.heading;
                int tries = 4;
                while (--tries != -1) {
                    if (!turnBlocked(slot)) {
                        break;
                    }
                    car.heading -= turn / 4;
                    car.updateCorners();
                }
                if (tries == 0) {
                    car.heading = heading;
                    car.updateCorners();
                }
                if (slot != 0) {
                    cars_[static_cast<std::size_t>(slot)].steer = 0;
                }
            }
        }
        if (slot == 0) {
            player.prepareMove(pieces, events.player);
            continue;
        }
        car.startStep();
        bool bump = false;
        if (hitRoadWalls(pieces, car, bump) && bump) {
            events.wallBumps.push_back(car.position);
        }
    }

    // The collision pass: each car against the partners, then it moves.
    for (const int slot : movers_) {
        if (slot != 0 && !cars_[static_cast<std::size_t>(slot)].active) {
            continue;
        }
        CarBody& car = body(slot);
        car.updateCorners();
        for (const int other : partners_) {
            // Signed: every car ahead is tested, and those up to one behind.
            if (other == slot || car.piece - body(other).piece >= 2) {
                continue;
            }
            body(other).updateCorners();
            collideCars(car, body(other), true, &events.crashes);
        }
        if (slot == 0) {
            player.applyStep();
            continue;
        }
        car.position.x = car.step.x + car.position.x;
        car.position.z = car.step.z + car.position.z;
        car.updateCorners();
    }
    player.endMove();
}

std::array<int, 6> Traffic::places(std::span<const RoadPiece> pieces) const
{
    std::array<int, 6> result{1, 2, 3, 4, 5, 6};
    if (cars_.size() < 6 || pieces.empty()) {
        return result;
    }
    // The key is the piece times 100 plus the distance along the piece.
    std::array<int, 6> keys{};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const CarBody& car = body(static_cast<int>(i));
        const auto index = static_cast<std::size_t>(std::clamp(car.piece, 0, pieceCount_ - 1));
        const RoadPiece& road = pieces[index];
        const int h = road.heading & angleMask;
        const float along = (car.position.x - road.centre.position.x) * angleCos(h) +
                            (car.position.z - road.centre.position.z) * angleSin(h);
        keys[i] = car.piece * 100 + roundToInt(along);
    }
    std::array<int, 6> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto rank = static_cast<int>(std::find(sorted.begin(), sorted.end(), keys[i]) - sorted.begin());
        result[i] = forward_ ? 6 - rank : rank + 1;
    }
    return result;
}

bool Traffic::placeSign(const std::array<int, 6>& ranks, int place, Mat4& matrix) const
{
    if (cars_.size() < ranks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < ranks.size(); ++i) {
        if (ranks[i] != place) {
            continue;
        }
        const CarBody& car = body(static_cast<int>(i));
        // 10 units over the car (0x408481..), facing its heading.
        const Vec3 position{car.position.x, car.position.y + 10.0F, car.position.z};
        matrix = rotationY(0x3ff - (car.heading & angleMask)) * Mat4::translation(position);
        return true;
    }
    return false;
}

// The computer car callback (0x40654b) and its road attitude (0x405058).
void Traffic::runCallbacks(std::span<const RoadPiece> pieces)
{
    for (std::size_t slot = 1; slot < cars_.size(); ++slot) {
        TrafficCar& car = cars_[slot];
        if (!car.callbacks) {
            continue;
        }
        CarBody& b = car.body;
        const int angle = 0x3ff - (b.heading & angleMask);
        int piece = findPiece(pieces, b.piece, b.position.x, b.position.z);
        if (piece < 0) {
            piece = std::clamp(b.piece, 0, pieceCount_ - 1);
        }
        const RoadPiece& road = pieces[static_cast<std::size_t>(piece)];
        const float x = b.position.x;
        const float z = b.position.z;
        const float y = roadHeightAt(road, x, z);
        // 8 units along the angle (_DAT_00441d3d) and 4 to either side
        // (_DAT_00441d49); 128 times the normalised rise (_DAT_00441d41).
        const float ax = angleCos(angle) * 8.0F;
        const float az = angleSin(angle) * 8.0F;
        const Vec3 ahead = normalise({ax, roadHeightAt(road, x + ax, z + az) - y, az});
        car.pitch = roundToInt(ahead.y * 128.0);
        const int sideAngle = (angle + 0x100) & angleMask;
        const float sx = angleCos(sideAngle) * 4.0F;
        const float sz = angleSin(sideAngle) * 4.0F;
        const Vec3 across = normalise(
            {2.0F * sx, roadHeightAt(road, x + sx, z + sz) - roadHeightAt(road, x - sx, z - sz),
             2.0F * sz});
        car.roll = roundToInt(across.y * 128.0);
        b.position.y = y;
        car.matrix = rotationZ(car.roll) *
                     (rotationX(-car.pitch) * (rotationY(angle) * Mat4::translation(b.position)));
    }
}

void Traffic::drive(int driverIndex, int raceState, Lcg& random, TrafficEvents& events)
{
    if (driverIndex < 0 || driverIndex >= static_cast<int>(drivers_.size())) {
        return;
    }
    const Driver& driver = drivers_[static_cast<std::size_t>(driverIndex)];
    if (!driver.assigned) {
        return;
    }
    switch (driver.type) {
    case driver_kind::civilian:
        driveCivilian(driverIndex);
        break;
    case driver_kind::racer:
        driveRacer(driverIndex);
        break;
    case driver_kind::police:
        drivePolice(driverIndex, raceState, random, events);
        break;
    default:
        break;
    }
}

// fill_front_array (0x40d5d2) with (9, 3) and the lane status of
// FUN_0040d80e for the own lane and its neighbours.
Traffic::LaneView Traffic::viewLanes(int slot) const
{
    const TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    LaneView view;
    const int step = car.body.forward ? 1 : -1;
    view.edge = car.body.forward ? 0 : 10;
    view.inner = car.body.forward ? 3 : 7;
    view.front.fill(-1);
    view.back.fill(-1);
    const auto wrap = [this](int piece) {
        while (piece < 0) {
            piece += pieceCount_;
        }
        while (piece >= pieceCount_) {
            piece -= pieceCount_;
        }
        return piece;
    };
    const auto fill = [&](std::array<int, 11>& lanes, int count, int direction) {
        int piece = car.body.piece;
        for (int i = 0; i <= count; ++i) {
            for (int lane = 0; lane < 11; ++lane) {
                auto& entry = lanes[static_cast<std::size_t>(lane)];
                if (entry == -1) {
                    entry = laneEntry(wrap(piece), lane);
                    if (entry == slot) {
                        entry = -1;
                    }
                }
            }
            piece += direction;
        }
    };
    fill(view.front, 9, step);
    fill(view.back, 3, -step);

    const int own = car.body.lane;
    view.lanes = {own, own + step, own - step};
    const auto status = [&](int index) {
        const int lane = view.lanes[static_cast<std::size_t>(index)];
        int& other = view.other[static_cast<std::size_t>(index)];
        other = -1;
        if (lane != own && at(view.back, lane) != -1) {
            return 0;
        }
        other = at(view.front, lane);
        if (other == -1) {
            return 4;
        }
        const float r = range(car.body, body(other));
        if (r <= closeRange) {
            return 1;
        }
        return r <= farRange ? 2 : 3;
    };
    view.status[0] = status(0);
    if (own == view.inner || own == view.edge) {
        if (own == view.inner) {
            view.lanes[1] = view.lanes[2];
        }
        view.status[1] = status(1);
        view.count = 2;
    } else {
        view.status[1] = status(1);
        view.status[2] = status(2);
        view.count = 3;
    }
    return view;
}

// The EMPTY LANE tactic of ai_follow_waypoints and driver_type4_step: the
// best lane status wins, ties go to the lane nearer the preferred one. The
// FASTEST LANE tactic is never chosen (its flag stays 0).
void Traffic::chooseLane(int slot, const Driver& driver, const LaneView& view)
{
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    const auto& s = view.status;
    const auto distance = [&](int index) {
        return std::abs(driver.lane - view.lanes[static_cast<std::size_t>(index)]);
    };
    int chosen = 0;
    if (view.count == 2) {
        if (s[1] == s[0]) {
            if (s[1] > 1 && distance(1) < distance(0)) {
                chosen = 1;
            }
        } else if (s[1] > s[0]) {
            chosen = 1;
        }
    } else if (s[0] < s[1] || s[0] < s[2]) {
        if (s[2] == s[1]) {
            chosen = distance(1) > distance(2) ? 2 : 1;
        } else {
            chosen = s[2] > s[1] ? 2 : 1;
        }
    } else if (s[0] == s[1]) {
        chosen = distance(1) < distance(0) ? 1 : 0;
    } else if (s[2] == s[0]) {
        chosen = distance(0) > distance(2) ? 2 : 0;
    }
    if (chosen != 0) {
        car.targetLane = view.lanes[static_cast<std::size_t>(chosen)];
    }
    // "Speeding" with room ahead, else the speed of the car ahead.
    const int ahead = view.other[static_cast<std::size_t>(chosen)];
    if (s[static_cast<std::size_t>(chosen)] >= 2 || ahead < 0) {
        car.topSpeed = driver.speed;
    } else {
        car.topSpeed = body(ahead).speed;
    }
}

// ai_follow_waypoints (0x40e301): the computer racers.
void Traffic::driveRacer(int driverIndex)
{
    Driver& driver = drivers_[static_cast<std::size_t>(driverIndex)];
    TrafficCar& car = cars_[static_cast<std::size_t>(driver.car)];
    const LaneView view = viewLanes(driver.car);
    if (driver.target < 0 || driver.target >= static_cast<int>(racers_.size())) {
        return;
    }
    RacerEntry& racer = racers_[static_cast<std::size_t>(driver.target)];
    if (racer.stoppedBy != -1) {
        // Stopped by the police: down to 2.0, then 100 frames.
        if (car.topSpeed != stoppedSpeed) {
            car.topSpeed *= 0.5F;
            if (car.topSpeed < stoppedSpeed) {
                car.topSpeed = stoppedSpeed;
            }
            return;
        }
        if (racer.counter == 0) {
            return;
        }
        if (--racer.counter == 0) {
            racer.stoppedBy = -1;
        }
        return;
    }
    if (racer.state == 4) {
        // Past the finish: to the edge lane at the finish counter's speed;
        // stopped, its object callback is switched off. 0x40674a switches it
        // on again at the end of each frame, but this runs first in the next
        // one, so the port leaves it off.
        driver.lane = view.edge;
        driver.speed = static_cast<float>(racer.counter);
        if (car.topSpeed == 0.0F) {
            car.callbacks = false;
        }
    }
    chooseLane(driver.car, driver, view);
}

// drive_opponent case 2 (0x40ed35): the civilians keep to their lane and
// overtake on the lane beside it.
void Traffic::driveCivilian(int driverIndex)
{
    const Driver& driver = drivers_[static_cast<std::size_t>(driverIndex)];
    TrafficCar& car = cars_[static_cast<std::size_t>(driver.car)];
    const LaneView view = viewLanes(driver.car);
    const int lane = car.body.lane;
    const int step = car.body.forward ? 1 : -1;
    const auto speedOf = [this](int slot) { return body(slot).speed; };
    // 0x40f1e4: at most the driver's speed, at least 0; 0x40f1f2: at least 0.
    const auto follow = [&](int slot) {
        car.topSpeed = std::min(speedOf(slot), driver.speed);
        car.topSpeed = std::max(car.topSpeed, 0.0F);
    };
    const auto keep = [&](int slot) { car.topSpeed = std::max(speedOf(slot), 0.0F); };
    const auto cruise = [&] { car.topSpeed = driver.speed; };
    const auto change = [&](int to) {
        car.targetLane = to;
        cruise();
    };

    const int ahead = at(view.front, lane);
    if (driver.lane == lane) {
        // In the slow lane.
        if (ahead == -1) {
            cruise();
            return;
        }
        const float r = range(body(ahead), car.body);
        if (r > closeRange) {
            if (r >= farRange || speedOf(ahead) < driver.speed) {
                cruise();
            } else {
                follow(ahead);
            }
            return;
        }
        // Too close: overtake on lane + step when it is free.
        const int other = lane + step;
        if (speedOf(ahead) >= driver.speed || car.body.speed < speedOf(ahead)) {
            follow(ahead);
        } else if (at(view.back, other) != -1) {
            keep(ahead);
        } else if (at(view.front, other) == -1) {
            change(other);
        } else if (range(body(at(view.front, other)), car.body) <= closeRange) {
            keep(ahead);
        } else {
            change(other);
        }
        return;
    }

    // In the fast lane: back to lane - step when there is room.
    const int other = lane - step;
    if (ahead == -1) {
        const int beside = at(view.front, other);
        if (at(view.back, other) != -1) {
            cruise();
        } else if (beside == -1) {
            change(other);
        } else if (speedOf(beside) < driver.speed) {
            cruise();
        } else {
            const float r = range(body(beside), car.body);
            if (r <= closeRange) {
                cruise();
            } else if (r >= farRange) {
                change(other);
            } else {
                car.targetLane = other;
                follow(beside);
            }
        }
        return;
    }
    const float r = range(body(ahead), car.body);
    if (r <= closeRange) {
        const int beside = at(view.front, other);
        if (at(view.back, other) != -1) {
            follow(ahead);
        } else if (beside == -1) {
            change(other);
        } else if (speedOf(beside) > car.body.speed ||
                   range(body(ahead), car.body) > overtakeRange) {
            car.targetLane = other;
            keep(beside);
        } else {
            follow(ahead);
        }
        return;
    }
    if (r >= farRange) {
        cruise();
        return;
    }
    if (speedOf(ahead) < driver.speed) {
        keep(ahead);
        return;
    }
    if (at(view.back, other) != -1) {
        follow(ahead);
        return;
    }
    // The binary reads back[other] here, which is -1, so a record before
    // the table; the port takes the car ahead in that lane [guess].
    const int beside = at(view.front, other);
    if (beside == -1) {
        change(other);
        return;
    }
    if (speedOf(beside) < driver.speed) {
        cruise();
        return;
    }
    const float besideRange = range(body(beside), car.body);
    if (besideRange <= closeRange) {
        follow(ahead);
    } else if (besideRange >= farRange) {
        change(other);
    } else {
        car.targetLane = other;
        follow(beside);
    }
}

// driver_type4_step (0x40d8aa): stake-out, chase and cool-down.
void Traffic::drivePolice(int driverIndex, int raceState, Lcg& random, TrafficEvents& events)
{
    Driver& driver = drivers_[static_cast<std::size_t>(driverIndex)];
    const int slot = driver.car;
    TrafficCar& car = cars_[static_cast<std::size_t>(slot)];
    if (driver.target < 0 || driver.target >= static_cast<int>(police_.size())) {
        return;
    }
    PoliceEntry& police = police_[static_cast<std::size_t>(driver.target)];
    const int step = car.body.forward ? 1 : -1;

    if (police.state == police_state::stakeOut) {
        car.topSpeed = 0.0F;
        // heading_neighbours (0x40d71c): racers in the lanes of the driving
        // direction, up to six pieces on.
        std::array<bool, 6> near{};
        int piece = car.body.piece;
        const int first = car.body.forward ? 0 : 6;
        for (int k = 0; k <= 6; ++k) {
            int wrapped = piece < 0 ? pieceCount_ + piece : piece;
            if (wrapped >= pieceCount_) {
                wrapped -= pieceCount_;
            }
            for (int lane = first; lane <= first + 4; ++lane) {
                const int entry = laneEntry(wrapped, lane);
                if (entry >= 0 && entry < 6) {
                    near[static_cast<std::size_t>(entry)] = true;
                }
            }
            piece += step;
        }
        for (int racer = 0; racer < 6; ++racer) {
            if (!near[static_cast<std::size_t>(racer)]) {
                continue;
            }
            police.fastEnough = body(racer).speed > chaseSpeed ? 1 : 0;
            if (police.fastEnough == 0) {
                continue;
            }
            const auto chasers = std::count_if(police_.begin(), police_.end(),
                                               [racer](const PoliceEntry& p) { return p.target == racer; });
            if (chasers >= 2) {
                continue;
            }
            if (racer == 0) {
                if (random.next() % 3 == 0) {
                    continue;
                }
            } else {
                // Only even police cars, and only when rand() is 0: the
                // computer racers are almost never chased.
                if (driver.target % 2 != 0 || random.next() != 0 || raceState != 0) {
                    continue;
                }
            }
            police.state = police_state::chase;
            police.target = racer;
            police.speed = roundToInt(driver.speed);
            police.lane = driver.lane;
            car.topSpeed = driver.speed;
            return;
        }
        return;
    }

    if (police.state == police_state::chase) {
        const CarBody& target = body(police.target);
        if (target.piece == car.body.piece || step * target.piece > step * car.body.piece) {
            // Behind the target: catch up at the chase speed.
            driver.speed = static_cast<float>(police.speed);
            driver.lane = target.lane;
        } else if (target.lane == car.body.lane) {
            // In front in its lane: let it come up and stop it.
            driver.speed = static_cast<float>(target.speed + -1.0);
            if (driver.speed < 0.0F) {
                driver.speed = 0.0F;
            }
            driver.lane = target.lane;
            if (std::abs(car.body.piece - target.piece) <= 1) {
                const int caught = police.target;
                if (caught != 0) {
                    const TrafficCar& stopped = cars_[static_cast<std::size_t>(caught)];
                    const int index = drivers_[static_cast<std::size_t>(stopped.driver)].target;
                    if (index >= 0 && index < static_cast<int>(racers_.size())) {
                        racers_[static_cast<std::size_t>(index)].stoppedBy = slot;
                        racers_[static_cast<std::size_t>(index)].counter = stopFrames;
                    }
                } else {
                    events.caught = true;
                }
                for (auto& other : police_) {
                    if (other.target == caught) {
                        other.state = police_state::cooldown;
                        other.target = -1;
                        other.cooldown = policeCooldown;
                    }
                }
            }
        } else {
            // In front in another lane. The piece difference is compared
            // with 57600 (_DAT_00444ae0), so this always moves over.
            if (static_cast<float>(std::abs(target.piece - car.body.piece)) < closeRange) {
                car.targetLane = target.lane;
                car.topSpeed = std::max(target.speed, 0.0F);
                return;
            }
            driver.speed = target.speed;
            driver.lane = target.lane;
            car.topSpeed = std::max(car.topSpeed, 0.0F);
        }
    }

    const LaneView view = viewLanes(slot);
    if (police.state == police_state::cooldown) {
        if (car.body.lane == view.edge) {
            if (police.cooldown != 0) {
                car.topSpeed = static_cast<float>(car.topSpeed * 0.99);
                --police.cooldown;
                if (view.status[0] < 2 && view.other[0] >= 0) {
                    car.topSpeed = body(view.other[0]).speed;
                }
                return;
            }
            car.topSpeed = 0.0F;
            police.state = police_state::stakeOut;
            return;
        }
        car.targetLane = view.edge;
        police.cooldown = policeCooldown;
        return;
    }
    chooseLane(slot, driver, view);
}

} // namespace opena2::race
