#pragma once

// The race's sound rules (A2SPEL.DAT sound.cpp, snd_fx.cpp): the sample
// table, the engine voice tables, and the volumes from INFIO.INI.

#include "opena2/menu_data.hpp"

#include <array>

namespace opena2::race {

// s_motor2_wav_00458db4: samples 0..47 for start_music and
// play_sound_effect.
const std::array<const char*, 48>& sampleNames() noexcept;

// s_motor_wav_00457758: the player's four engine voices.
namespace engine_voice {
constexpr int low = 0;
constexpr int high = 1;
constexpr int gearUp = 2;
constexpr int gearDown = 3;
} // namespace engine_voice
const std::array<const char*, 4>& engineSampleNames() noexcept;

// A frequency in Hz and an attenuation in 1/100 dB.
struct VoiceSetting {
    int frequency = 0;
    int volume = 0;
};

// load_control_keys (0x402ac7): the high voice (DAT_004729c8) and the low
// voice (DAT_00472ce8), indexed by the engine sound index 0..79.
const std::array<VoiceSetting, 80>& engineHighTable() noexcept;
const std::array<VoiceSetting, 80>& engineLowTable() noexcept;
// FUN_00403273: gas adds this to the high voice.
constexpr VoiceSetting gasBoost{6000, 300};

// load_a2_ini (0x40c2f4): GELUID and the two volumes.
struct SoundSettings {
    // DAT_00458c3c and DAT_00458c40.
    bool music = true;
    bool effects = true;
    // DAT_00458c30, DAT_00458c48 (engine only) and DAT_00458c34: amounts
    // taken off every volume.
    int effectsAttenuation = 0;
    int engineAttenuation = 0;
    int musicAttenuation = 0;

    static SoundSettings fromInfio(const Infio& infio) noexcept;

    // start_music and play_sound_effect.
    int musicVolume() const noexcept { return -musicAttenuation - 100; }
    int effectVolume() const noexcept { return -effectsAttenuation - 100; }
    // The engine voices: `volume` from the tables, less both master values.
    int engineVolume(int volume) const noexcept
    {
        return volume - effectsAttenuation - engineAttenuation;
    }
};

// The distance table of FUN_00410168: entry i = -500 - 7 * i, i < 500.
constexpr int distanceVolume(int distance) noexcept { return -500 - 7 * distance; }

} // namespace opena2::race
