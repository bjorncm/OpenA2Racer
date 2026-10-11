#pragma once

#include "opena2/pcm_wave.hpp"

#include <SDL3/SDL_audio.h>

#include <array>
#include <cstdint>
#include <span>

namespace opena2::game {

// DirectSound-style sound buffers as the original menu uses them
// (sound.cpp / mysound.cpp): 40 buffers, each started sound returns a
// handle, and volumes are attenuations in hundredths of a decibel.
class SoundSystem {
public:
    static constexpr int bufferCount = 40;
    static constexpr int none = -1;

    SoundSystem() = default;
    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;
    ~SoundSystem();

    bool initialize();
    bool available() const noexcept { return device_ != 0; }

    // play_sound_buffer: starts `wave` in a free buffer at full volume.
    int play(const PcmWave& wave, bool looping);
    void stop(int handle);
    bool busy(int handle) const;
    void setVolume(int handle, int hundredthsOfDecibel);
    // IDirectSoundBuffer::SetFrequency: the playback rate in Hz.
    void setFrequency(int handle, int hertz);
    // stop_all_sounds.
    void stopAll();

    // Raw PCM stream for movie soundtracks.
    void startStream(int rate, int channels, int bits);
    void queueStream(std::span<const std::uint8_t> samples);
    void stopStream();

    // Keeps looping buffers fed; call once per frame.
    void update();

private:
    struct Buffer {
        SDL_AudioStream* stream = nullptr;
        const PcmWave* wave = nullptr;
        bool looping = false;
    };

    std::array<Buffer, bufferCount> buffers_{};
    SDL_AudioStream* movie_ = nullptr;
    SDL_AudioDeviceID device_ = 0;
    int next_ = 0;
};

} // namespace opena2::game
