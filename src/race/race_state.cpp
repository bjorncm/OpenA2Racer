#include "opena2/race/race_state.hpp"

#include <algorithm>
#include <utility>

namespace opena2::race {
namespace {

// The fine ladder at 0x459110 (advance_stage_counter, 0x412183).
constexpr std::array<std::int32_t, 6> fines{250, 500, 800, 1200, 1500, 2000};

} // namespace

bool raceOver(int playerPiece, int pieceCount, bool forward) noexcept
{
    return forward ? playerPiece == pieceCount - 15 : playerPiece == 15;
}

bool placementsValid(const Infio& infio) noexcept
{
    std::array<int, 6> places{infio.position};
    std::copy(infio.racerPosition.begin(), infio.racerPosition.end(), places.begin() + 1);
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (places[i] < 1 || places[i] > 6) {
            return false;
        }
        for (std::size_t j = 0; j < places.size(); ++j) {
            if (i != j && places[i] == places[j]) {
                return false;
            }
        }
    }
    return true;
}

void ensureValidPlacements(Infio& infio) noexcept
{
    if (!placementsValid(infio)) {
        infio.position = 6;
        for (std::size_t i = 0; i < infio.racerPosition.size(); ++i) {
            infio.racerPosition[i] = static_cast<std::int16_t>(i + 1);
        }
    }
}

RaceRules::RaceRules(const Infio& infio, std::int32_t clockSeconds, std::int32_t bonusSeconds,
                     std::int32_t now)
    : endReason_(infio.finishedBy)
    , bonusSeconds_(bonusSeconds)
{
    // set_race_clock (0x411e08): every racer starts now, without a time.
    clock_ = clockSeconds;
    clockReference_ = now;
    for (auto& racer : racers_) {
        racer = {now, 0, 0};
    }
    money_ = infio.money;
    if (money_ < 1 || money_ > maxMoney) {
        money_ = 1000;
    }
    // "Done init - starting loop": state 1 and race_countdown_start
    // (0x4116cb), which puts the camera at position 1.
    state_ = race_state::flyIn;
    flyInStarted_ = true;
    flyIn_ = flyInStart;
    events_.camera = 1;
}

void RaceRules::setState(int state, std::int32_t now)
{
    // Any state may be entered from 0; only 0 and 8 from the others; 8 never
    // changes. The actions below run for the resulting state, even when the
    // state did not change.
    if ((state_ == race_state::racing || state == race_state::racing ||
         state == race_state::exit) &&
        state_ != race_state::exit) {
        state_ = state;
    }
    switch (state_) {
    case race_state::racing:
        clockRunning_ = true;
        playerControl_ = true;
        break;
    case race_state::caught:
        fine_ = fines[fineStep_];
        fineStep_ = std::min<std::size_t>(fineStep_ + 1, fines.size() - 1);
        startHold(7, now);
        play(race_sound::caught);
        break;
    case race_state::timeOut:
    case race_state::fuelOut:
        stopCar();
        startHold(7, now);
        playerControl_ = false;
        break;
    case race_state::moneyOut:
        startHold(12, now);
        playerControl_ = false;
        break;
    case race_state::paused:
        clockRunning_ = false;
        playerControl_ = false;
        break;
    default:
        break;
    }
}

void RaceRules::dispatch(const RaceKeys& keys, std::int32_t now)
{
    switch (state_) {
    case race_state::flyIn:
        if (!flyInStarted_) {
            flyInStarted_ = true;
            flyIn_ = flyInStart;
            events_.camera = 1;
        }
        flyIn_ += flyInStep;
        if (flyIn_ <= 0.0F) {
            events_.camera = 2;
            state_ = race_state::startLights;
        }
        break;
    case race_state::startLights:
        if (!startLightsStarted_) {
            // start_countdown_steps (0x4117bb).
            startStep_ = 0;
            startHold(1, now);
            startLightsStarted_ = true;
        }
        if (holdExpired(now)) {
            const int step = startStep_ + 1;
            if (step == 5) {
                clockRunning_ = true;
                playerControl_ = true;
                state_ = race_state::racing;
                startStep_ = 4;
            } else {
                startStep_ = step;
                play(step < 4 ? race_sound::startStep : race_sound::startGo);
                startHold(1, now);
            }
        }
        break;
    case race_state::finished:
        if (!resultStarted_) {
            // finish_result_setup (0x41185e).
            stopCar();
            playerControl_ = false;
            startHold(10, now);
            resultStarted_ = true;
        }
        if (holdExpired(now) || keys.space || keys.enter) {
            state_ = race_state::exit;
        }
        // stage_hud_element(DAT_00475a28), then the counter steps mod 16.
        finishFrame_ = finishCounter_;
        finishCounter_ = (finishCounter_ + 1) % 16;
        break;
    case race_state::caught:
        if (holdExpired(now) || keys.enter || keys.space) {
            setState(race_state::racing, now);
            drainMoney(fine_, now);
        }
        break;
    case race_state::timeOut:
    case race_state::fuelOut:
        if (holdExpired(now)) {
            setState(race_state::exit, now);
        }
        break;
    case race_state::moneyOut:
        if (holdExpired(now) || keys.enter || keys.space) {
            setState(race_state::exit, now);
        }
        break;
    case race_state::paused:
        // The caller waits for P to be released first.
        if (keys.p || keys.enter || keys.space) {
            clockReference_ = now;
            setState(race_state::racing, now);
        }
        break;
    default:
        break;
    }
}

void RaceRules::tickClock(std::int32_t now)
{
    if (!clockRunning_) {
        clockReference_ = now;
    }
    // race_clock_tick (0x411ff3).
    if (clock_ != 0) {
        clock_ -= now - clockReference_;
        clockReference_ = now;
        if (clock_ < 1) {
            endReason_ = end_reason::time;
            finished_ = false;
            setState(race_state::timeOut, now);
            clock_ = 0;
            play(race_sound::timeOut);
        }
    }
}

void RaceRules::useFuel(std::int32_t amount, std::int32_t now)
{
    fuel_ -= amount;
    if (fuel_ < 1) {
        endReason_ = end_reason::fuel;
        finished_ = false;
        setState(race_state::fuelOut, now);
        play(race_sound::fuelOut);
        fuel_ = 0;
    }
}

void RaceRules::drainMoney(std::int32_t amount, std::int32_t now)
{
    money_ -= amount;
    if (money_ < 0) {
        endReason_ = end_reason::money;
        finished_ = false;
        setState(race_state::moneyOut, now);
        play(race_sound::moneyOut);
        money_ = 0;
    }
}

void RaceRules::addTimeBonus()
{
    clock_ += bonusSeconds_;
    play(race_sound::timeBonus);
}

void RaceRules::addFuel()
{
    // An empty tank stays empty.
    if (fuel_ != 0) {
        fuel_ = std::min(fuel_ + 75, maxFuel);
    }
    play(race_sound::fuelBonus);
}

void RaceRules::addMoney()
{
    money_ = std::min(money_ + 250, maxMoney);
    play(race_sound::moneyBonus);
}

void RaceRules::escape()
{
    if (state_ == race_state::racing) {
        endReason_ = end_reason::none;
        finished_ = false;
        state_ = race_state::exit;
    }
}

void RaceRules::checkFinish(bool over, std::int32_t now)
{
    if (over && state_ != race_state::exit && state_ != race_state::finished) {
        setFinishTime(0, now);
        play(race_sound::finish);
        state_ = race_state::finished;
        finished_ = true;
    }
}

void RaceRules::setFinishTime(int racer, std::int32_t now)
{
    Racer& entry = racers_[static_cast<std::size_t>(std::clamp(racer, 0, 5))];
    if (entry.order == 0) {
        entry.finish = now - entry.start;
        entry.order = ++finishedCount_;
    }
}

std::int32_t RaceRules::finishTime(int racer, Lcg& random) const
{
    const Racer& entry = racers_[static_cast<std::size_t>(std::clamp(racer, 0, 5))];
    if (entry.order != 0) {
        return entry.finish;
    }
    return racers_[0].finish + random.next() % 60 + 5;
}

std::array<int, 6> RaceRules::resultPlaces() const
{
    std::array<int, 6> racers{0, 1, 2, 3, 4, 5};
    std::stable_sort(racers.begin(), racers.end(), [this](int a, int b) {
        const Racer& ra = racers_[static_cast<std::size_t>(a)];
        const Racer& rb = racers_[static_cast<std::size_t>(b)];
        if ((ra.order != 0) != (rb.order != 0)) {
            return ra.order != 0;
        }
        if (ra.order != 0) {
            return ra.order < rb.order;
        }
        return places_[static_cast<std::size_t>(a)] < places_[static_cast<std::size_t>(b)];
    });
    std::array<int, 6> places{};
    for (std::size_t i = 0; i < racers.size(); ++i) {
        places[static_cast<std::size_t>(racers[i])] = static_cast<int>(i) + 1;
    }
    return places;
}

int RaceRules::finishPlace(int racer) const
{
    return resultPlaces()[static_cast<std::size_t>(std::clamp(racer, 0, 5))];
}

void RaceRules::writeResult(Infio& infio, Lcg& random) const
{
    infio.finishedBy = endReason_;
    if (!finished_) {
        infio.time = 0;
        infio.raceFinished = 0;
        return;
    }
    infio.time = finishTime(0, random);
    infio.money = money_;
    infio.position = static_cast<std::int16_t>(finishPlace(0));
    for (std::size_t i = 0; i < infio.racerPosition.size(); ++i) {
        infio.racerPosition[i] = static_cast<std::int16_t>(finishPlace(static_cast<int>(i) + 1));
    }
    for (std::size_t i = 0; i < infio.racerTime.size(); ++i) {
        infio.racerTime[i] = finishTime(static_cast<int>(i) + 1, random);
    }
    infio.raceFinished = 1;
}

RaceEvents RaceRules::takeEvents()
{
    return std::exchange(events_, {});
}

void RaceRules::startHold(std::int32_t seconds, std::int32_t now)
{
    hold_ = seconds;
    holdReference_ = now;
}

// hold_expired (0x408cff).
bool RaceRules::holdExpired(std::int32_t now)
{
    if (hold_ != 0) {
        hold_ -= now - holdReference_;
        holdReference_ = now;
        if (hold_ < 1) {
            hold_ = 0;
        }
    }
    return hold_ == 0;
}

void RaceRules::stopCar()
{
    events_.stopCar = true;
}

} // namespace opena2::race
