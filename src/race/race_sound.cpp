#include "opena2/race/race_sound.hpp"

namespace opena2::race {

const std::array<const char*, 48>& sampleNames() noexcept
{
    static constexpr std::array<const char*, 48> names{
        "motor2.wav", "motor3.wav", "speed.wav", "energy.wav", "jammin.wav", "moody.wav",
        "disco.wav", "rock.wav", "sirene.wav", "bump.wav", "bump2.wav", "bump3.wav",
        "bump4.wav", "start.wav", "mstart.wav", "truck01.wav", "truck02.wav", "truck03.wav",
        "truck05.wav", "truck06.wav", "toet01.wav", "toet03.wav", "toet05.wav", "toet06.wav",
        "autos1.wav", "autos2.wav", "set.wav", "go.wav", "sp_b1.wav", "sp_b2.wav",
        "sp_b3.wav", "sp_p1.wav", "sp_p2.wav", "sp_p3.wav", "sp_p4.wav", "sp_p5.wav",
        "sp_p6.wav", "sp_p7.wav", "sp_p8.wav", "sp_p9.wav", "bonusb.wav", "bonusg.wav",
        "bonust.wav", "finish.wav", "loser.wav", "sp_r1.wav", "sp_r2.wav", "sp_r3.wav"};
    return names;
}

const std::array<const char*, 4>& engineSampleNames() noexcept
{
    static constexpr std::array<const char*, 4> names{"motor.wav", "motor1.wav", "gearup.wav",
                                                      "geardwn.wav"};
    return names;
}

const std::array<VoiceSetting, 80>& engineHighTable() noexcept
{
    // Entries 0..34: 11025 + i Hz, -2000 + 45 * i; then 400 Hz a step from
    // 11060 Hz at -515.
    static const auto table = [] {
        std::array<VoiceSetting, 80> t{};
        for (int i = 0; i < 35; ++i) {
            t[static_cast<std::size_t>(i)] = {11025 + i, -2000 + 45 * i};
        }
        for (int i = 35; i < 80; ++i) {
            t[static_cast<std::size_t>(i)] = {11060 + 400 * (i - 35), -515};
        }
        return t;
    }();
    return table;
}

const std::array<VoiceSetting, 80>& engineLowTable() noexcept
{
    // 5500 Hz throughout, fading by 42 a step to -1528, then -5000.
    static const auto table = [] {
        std::array<VoiceSetting, 80> t{};
        for (int i = 0; i < 35; ++i) {
            t[static_cast<std::size_t>(i)] = {5500, -100 - 42 * i};
        }
        for (int i = 35; i < 80; ++i) {
            t[static_cast<std::size_t>(i)] = {5500, -5000};
        }
        return t;
    }();
    return table;
}

SoundSettings SoundSettings::fromInfio(const Infio& infio) noexcept
{
    SoundSettings settings;
    // The defaults in the data section: music off, effects on.
    settings.music = false;
    settings.effects = true;
    switch (infio.sound) {
    case 0:
        settings.music = true;
        settings.effects = true;
        break;
    case 1:
        settings.music = false;
        settings.effects = true;
        break;
    case 2:
        settings.music = false;
        settings.effects = false;
        break;
    case 3:
        settings.music = true;
        settings.effects = false;
        break;
    default:
        break;
    }
    settings.effectsAttenuation = (4 - infio.effectsVolume) * 500;
    settings.engineAttenuation = (5 - infio.effectsVolume) * 300;
    settings.musicAttenuation = (4 - infio.musicVolume) * 500;
    return settings;
}

} // namespace opena2::race
