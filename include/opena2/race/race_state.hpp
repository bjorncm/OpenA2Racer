#pragma once

// The race state machine of A2SPEL.DAT: the state at 0x458c64 with its
// setter and per-frame dispatcher, the hold timer, the race clock, fuel and
// money counters, the per-racer finish table and the INFIO.INI result.
//
// All times are whole seconds of wall time, as clock_seconds (0x4290b9)
// returns them; the caller passes the current time to every call.

#include "opena2/menu_data.hpp"
#include "opena2/race/race_math.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace opena2::race {

// Values of the race state (DAT_00458c64).
namespace race_state {
constexpr int racing = 0;
// Camera fly-in before the start (race_countdown_start).
constexpr int flyIn = 1;
// Five one-second steps of the start lights.
constexpr int startLights = 2;
// The player crossed the finish; the result shows.
constexpr int finished = 3;
// Stopped by the police; the fine shows.
constexpr int caught = 4;
constexpr int timeOut = 5;
constexpr int moneyOut = 6;
constexpr int fuelOut = 7;
// Leave the race loop. Sticky.
constexpr int exit = 8;
constexpr int paused = 9;
} // namespace race_state

// EINDE_DOOR: why the race ended without a finish.
namespace end_reason {
constexpr std::int16_t none = 0;
constexpr std::int16_t fuel = 1;
constexpr std::int16_t money = 2;
constexpr std::int16_t time = 3;
} // namespace end_reason

// Sample numbers the rules play (play_sound_effect).
namespace race_sound {
constexpr int startStep = 0x1a;
constexpr int startGo = 0x1b;
constexpr int timeBonus = 0x1c;
constexpr int fuelBonus = 0x1d;
constexpr int moneyBonus = 0x1e;
constexpr int caught = 0x23;
constexpr int moneyOut = 0x25;
constexpr int timeOut = 0x26;
constexpr int fuelOut = 0x27;
constexpr int finish = 0x2b;
} // namespace race_sound

constexpr std::int32_t maxFuel = 300;
constexpr std::int32_t maxMoney = 9999;
constexpr float flyInStart = 350.0F;
// _DAT_00446180, added to the fly-in counter once per frame.
constexpr float flyInStep = -10.0F;

// Keys the dispatcher reads (key_pressed, 0x42379c: held, Shift up).
struct RaceKeys {
    bool enter = false;
    bool space = false;
    bool p = false;
};

// What a call asked of the rest of the game, in order.
struct RaceEvents {
    std::vector<int> sounds;
    // set_camera_position argument, 0 when none.
    int camera = 0;
    // clear_car_lateral / finish_result_setup: stop the player car.
    bool stopCar = false;
};

// The race is over when the player reaches the piece 15 pieces before the
// end of the driving direction (race_over, 0x411258).
bool raceOver(int playerPiece, int pieceCount, bool forward) noexcept;

// placements_valid (0x40c278): the six starting places are 1..6 and distinct.
bool placementsValid(const Infio& infio) noexcept;
// load_a2_ini: invalid places become POSITIE 6 and RACER_1..5 = 1..5.
void ensureValidPlacements(Infio& infio) noexcept;

class RaceRules {
public:
    // load_a2_ini (0x40c2f4) and the start of the race loop: the clock is set
    // to `clockSeconds`, money comes from INFIO (1000 when outside 1..9999),
    // fuel is full, and the state is 1 (fly-in).
    RaceRules(const Infio& infio, std::int32_t clockSeconds, std::int32_t bonusSeconds,
              std::int32_t now);

    int state() const noexcept { return state_; }
    bool finished() const noexcept { return finished_; }
    // DAT_00458c60: the player may drive.
    bool playerControl() const noexcept { return playerControl_; }
    // DAT_00458c50: the race clock runs.
    bool clockRunning() const noexcept { return clockRunning_; }
    std::int32_t clock() const noexcept { return clock_; }
    std::int32_t fuel() const noexcept { return fuel_; }
    std::int32_t money() const noexcept { return money_; }
    std::int32_t fine() const noexcept { return fine_; }
    std::int16_t endReason() const noexcept { return endReason_; }
    float flyIn() const noexcept { return flyIn_; }
    // DAT_00475a24: start-light step 0..4.
    int startStep() const noexcept { return startStep_; }
    // The finish animation frame 0..15 the last state-3 dispatch drew
    // (DAT_00475a28 before its step).
    int finishFrame() const noexcept { return finishFrame_; }

    // set_state (0x40c6fe).
    void setState(int state, std::int32_t now);
    // state_dispatch (0x40c814), once per frame; state 9 is dispatched
    // again until it ends.
    void dispatch(const RaceKeys& keys, std::int32_t now);

    // draw_race_clock (0x40946e): the clock only drains while it runs.
    void tickClock(std::int32_t now);
    // fuel_tick (0x4120ae).
    void useFuel(std::int32_t amount, std::int32_t now);
    // money_drain (0x412130).
    void drainMoney(std::int32_t amount, std::int32_t now);
    // add_time_bonus, add_fuel, add_money (0x411fc7, 0x412078, 0x412101).
    void addTimeBonus();
    void addFuel();
    void addMoney();

    // The race loop's Esc key (0x1b): only while racing, it leaves the race
    // without a result.
    void escape();
    // The race loop's finish test: when `over` and the state is neither 3
    // nor 8, the player's finish time is taken and the result shows.
    void checkFinish(bool over, std::int32_t now);

    // Place of each racer (0 = player, 1..5 = computer racers) for
    // racer_rank_lookup; the opponents set it every frame.
    void setPlaces(const std::array<int, 6>& places) noexcept { places_ = places; }
    // racer_place: the current place of a racer, 1..6.
    int place(int racer) const noexcept { return places_[static_cast<std::size_t>(racer)]; }
    // The places shown on the HUD and on the signs over the cars: the cars
    // over the finish line keep their finishing place, the others follow by
    // their live place. Before anyone finishes these are the live places.
    // The original (FUN_00409383, the sign callbacks 0x408481..) shows the
    // live places throughout, so a player standing after the line drops to
    // 6/6 as the others pass and the signs swap; the port does not.
    std::array<int, 6> shownPlaces() const { return resultPlaces(); }
    int shownPlace() const { return resultPlaces()[0]; }
    // set_racer_finish_time (0x411ebb).
    void setFinishTime(int racer, std::int32_t now);
    // get_racer_finish_time (0x411f18): racers without a time get the
    // player's time plus 5..64 seconds.
    std::int32_t finishTime(int racer, Lcg& random) const;
    // get_racer_position (0x411f6a): the finishing place, 1..6, each once.
    // The original stores each car's live place at the moment it crosses
    // the line and gives the others their live place at the end. Because
    // the player's car stops dead on the line (finish_result_setup), a car
    // crossing beside it can get the same place, and the places written to
    // INFIO.INI repeat (the menu's stage result then shows the wrong
    // names). The port orders the cars that crossed the line by when they
    // did, then the rest by their live place.
    int finishPlace(int racer) const;

    // "Check if finisched OK" (0x401c30): the result written to INFIO.INI.
    // Only a finished race writes the time, places and money.
    void writeResult(Infio& infio, Lcg& random) const;

    RaceEvents takeEvents();

private:
    void startHold(std::int32_t seconds, std::int32_t now);
    bool holdExpired(std::int32_t now);
    void stopCar();
    void play(int sound) { events_.sounds.push_back(sound); }

    struct Racer {
        std::int32_t start = 0;
        std::int32_t finish = 0;
        // 1 for the first car over the line, 2 for the next...; 0 before.
        int order = 0;
    };

    std::array<int, 6> resultPlaces() const;

    int state_ = race_state::flyIn;
    bool finished_ = false;
    bool playerControl_ = false;
    bool clockRunning_ = false;
    std::int16_t endReason_ = end_reason::none;

    std::int32_t hold_ = 0;
    std::int32_t holdReference_ = 0;

    std::int32_t clock_ = 0;
    std::int32_t clockReference_ = 0;
    std::int32_t bonusSeconds_ = 300;
    std::int32_t fuel_ = maxFuel;
    std::int32_t money_ = 1000;
    std::size_t fineStep_ = 0;
    std::int32_t fine_ = 0;

    bool flyInStarted_ = false;
    float flyIn_ = 0.0F;
    bool startLightsStarted_ = false;
    int startStep_ = 0;
    bool resultStarted_ = false;
    int finishCounter_ = 0;
    int finishFrame_ = 0;

    std::array<Racer, 6> racers_{};
    int finishedCount_ = 0;
    std::array<int, 6> places_{1, 2, 3, 4, 5, 6};
    RaceEvents events_;
};

} // namespace opena2::race
