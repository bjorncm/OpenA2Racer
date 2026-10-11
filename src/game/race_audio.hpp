#pragma once

#include "race_assets.hpp"
#include "sound.hpp"

#include "opena2/race/race_sound.hpp"
#include "opena2/race/traffic.hpp"

#include <array>

namespace opena2::game {

// The race's sounds on the SoundSystem: the stage music (start_music,
// 0x410460), the effects (play_sound_effect, 0x4104bc) and the player's
// engine voices (start_race_sounds 0x402f3f, FUN_00403273, engine_pitch
// 0x402ed6). Volumes follow GELUID, VMUZIEK and VEFFECT.
class RaceAudio {
public:
    RaceAudio(SoundSystem& sound, RaceAssets& assets, const race::SoundSettings& settings);
    RaceAudio(const RaceAudio&) = delete;
    RaceAudio& operator=(const RaceAudio&) = delete;
    // "Stopping all sounds" after the race loop.
    ~RaceAudio();

    // start_music: a looped sample at the music volume.
    void startMusic(int sample);
    // play_sound_effect: sample 0..47 once.
    void effect(int sample);
    // FUN_004105b4 (bump3.wav, sample 11) and FUN_00410626 (bump.wav,
    // sample 9): at the distance volume, only within 500 units of the player.
    // sound_attenuation_a (bump2.wav, sample 10) lowers it by `extra` more.
    void bump(int sample, int distance, int extra = 0);
    // The engine loops of the cars near the player (FUN_004101f4,
    // sound_volume_by_table, stop_slot_sound): a slot whose car changed is
    // stopped and started again, the others follow the distance volume.
    void updateTraffic(const std::array<race::EngineSlot, 6>& slots);

    // start_race_sounds: both engine voices loop, at sound index 0 with the
    // gas boost on the high voice.
    void startEngine();
    // FUN_00403273's voice update for `soundIndex`.
    void updateEngine(int soundIndex, bool gas);
    // engine_pitch: the gear change sample.
    void changeGear(bool up);

private:
    void setVoice(int handle, const race::VoiceSetting& setting);

    SoundSystem& sound_;
    RaceAssets& assets_;
    race::SoundSettings settings_;
    std::array<int, 4> engine_{SoundSystem::none, SoundSystem::none, SoundSystem::none,
                               SoundSystem::none};
    struct TrafficVoice {
        int car = -1;
        int handle = SoundSystem::none;
    };
    std::array<TrafficVoice, 6> traffic_{};
};

} // namespace opena2::game
