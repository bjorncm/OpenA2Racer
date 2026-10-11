#include "race_audio.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstddef>

namespace opena2::game {
namespace {

// Development: SDL_LOGGING=audio=debug lists the race's sounds as they start.
void logSound(const char* what, const char* name, int volume)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "race sound: %s %s at %d", what, name, volume);
}

} // namespace

RaceAudio::RaceAudio(SoundSystem& sound, RaceAssets& assets, const race::SoundSettings& settings)
    : sound_(sound)
    , assets_(assets)
    , settings_(settings)
{
}

RaceAudio::~RaceAudio()
{
    sound_.stopAll();
}

void RaceAudio::startMusic(int sample)
{
    if (!settings_.music || sample < 0 || sample >= static_cast<int>(race::sampleNames().size())) {
        return;
    }
    if (const PcmWave* wave = assets_.sound(race::sampleNames()[static_cast<std::size_t>(sample)])) {
        sound_.setVolume(sound_.play(*wave, true), settings_.musicVolume());
        logSound("music", race::sampleNames()[static_cast<std::size_t>(sample)], settings_.musicVolume());
    }
}

void RaceAudio::effect(int sample)
{
    if (!settings_.effects || sample < 0 || sample >= static_cast<int>(race::sampleNames().size())) {
        return;
    }
    if (const PcmWave* wave = assets_.sound(race::sampleNames()[static_cast<std::size_t>(sample)])) {
        sound_.setVolume(sound_.play(*wave, false), settings_.effectVolume());
        logSound("effect", race::sampleNames()[static_cast<std::size_t>(sample)], settings_.effectVolume());
    }
}

void RaceAudio::bump(int sample, int distance, int extra)
{
    if (!settings_.effects || distance < 0 || distance >= 500) {
        return;
    }
    if (const PcmWave* wave = assets_.sound(race::sampleNames()[static_cast<std::size_t>(sample)])) {
        const int volume = race::distanceVolume(distance) - settings_.effectsAttenuation - extra;
        sound_.setVolume(sound_.play(*wave, false), volume);
        logSound("bump", race::sampleNames()[static_cast<std::size_t>(sample)], volume);
    }
}

void RaceAudio::updateTraffic(const std::array<race::EngineSlot, 6>& slots)
{
    if (!settings_.effects) {
        return;
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        TrafficVoice& voice = traffic_[i];
        const race::EngineSlot& slot = slots[i];
        if (voice.car != slot.car) {
            sound_.stop(voice.handle);
            voice = {};
            if (slot.car >= 0) {
                if (const PcmWave* wave =
                        assets_.sound(race::sampleNames()[static_cast<std::size_t>(slot.sample)])) {
                    voice = {slot.car, sound_.play(*wave, true)};
                    logSound("car loop", race::sampleNames()[static_cast<std::size_t>(slot.sample)],
                             race::distanceVolume(std::min(slot.distance, 499)));
                }
            }
        }
        if (voice.handle != SoundSystem::none) {
            // The distance table index is clamped to 0..499 here; the
            // original indexes entry 500 at exactly 500 units.
            sound_.setVolume(voice.handle,
                             race::distanceVolume(std::min(slot.distance, 499)) -
                                 settings_.effectsAttenuation);
        }
    }
}

void RaceAudio::startEngine()
{
    if (!settings_.effects) {
        return;
    }
    // start_engine_sound(1), then (0): set_sound_playing with flag 4, then
    // Play with EDX = 1 (DSBPLAY_LOOPING, 0x402f51).
    for (const int voice : {race::engine_voice::high, race::engine_voice::low}) {
        if (const PcmWave* wave = assets_.sound(race::engineSampleNames()[static_cast<std::size_t>(voice)])) {
            engine_[static_cast<std::size_t>(voice)] = sound_.play(*wave, true);
        }
    }
    const auto& high = race::engineHighTable()[0];
    setVoice(engine_[race::engine_voice::high],
             {high.frequency + race::gasBoost.frequency, high.volume + race::gasBoost.volume});
    setVoice(engine_[race::engine_voice::low], race::engineLowTable()[0]);
    logSound("engine", "motor.wav and motor1.wav", settings_.engineVolume(race::engineLowTable()[0].volume));
}

void RaceAudio::updateEngine(int soundIndex, bool gas)
{
    if (!settings_.effects || soundIndex < 0 || soundIndex >= 80) {
        return;
    }
    const auto index = static_cast<std::size_t>(soundIndex);
    race::VoiceSetting high = race::engineHighTable()[index];
    if (gas) {
        high.frequency += race::gasBoost.frequency;
        high.volume += race::gasBoost.volume;
    }
    setVoice(engine_[race::engine_voice::high], high);
    setVoice(engine_[race::engine_voice::low], race::engineLowTable()[index]);
}

void RaceAudio::changeGear(bool up)
{
    if (!settings_.effects) {
        return;
    }
    // FUN_0040291c: the sample once, then its volume.
    const int voice = up ? race::engine_voice::gearUp : race::engine_voice::gearDown;
    if (const PcmWave* wave = assets_.sound(race::engineSampleNames()[static_cast<std::size_t>(voice)])) {
        const int handle = sound_.play(*wave, false);
        engine_[static_cast<std::size_t>(voice)] = handle;
        sound_.setVolume(handle, settings_.engineVolume(-100));
        logSound("gear", race::engineSampleNames()[static_cast<std::size_t>(voice)],
                 settings_.engineVolume(-100));
    }
}

void RaceAudio::setVoice(int handle, const race::VoiceSetting& setting)
{
    sound_.setFrequency(handle, setting.frequency);
    sound_.setVolume(handle, settings_.engineVolume(setting.volume));
}

} // namespace opena2::game
