#include "sound.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace opena2::game {
namespace {

SDL_AudioSpec specFor(const PcmWave& wave)
{
    return {wave.format == PcmSampleFormat::unsigned8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE,
            wave.channels, static_cast<int>(wave.sampleRate)};
}

int bytesPerSecond(const SDL_AudioSpec& spec)
{
    return spec.freq * spec.channels * static_cast<int>(SDL_AUDIO_BYTESIZE(spec.format));
}

} // namespace

SoundSystem::~SoundSystem()
{
    for (auto& buffer : buffers_) {
        if (buffer.stream != nullptr) SDL_DestroyAudioStream(buffer.stream);
    }
    if (movie_ != nullptr) SDL_DestroyAudioStream(movie_);
    if (device_ != 0) SDL_CloseAudioDevice(device_);
}

bool SoundSystem::initialize()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Sound is unavailable: %s", SDL_GetError());
        return false;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_ == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open audio output: %s", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec initial{SDL_AUDIO_U8, 1, 22050};
    for (auto& buffer : buffers_) {
        buffer.stream = SDL_CreateAudioStream(&initial, nullptr);
        if (buffer.stream == nullptr || !SDL_BindAudioStream(device_, buffer.stream)) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
            return false;
        }
    }
    movie_ = SDL_CreateAudioStream(&initial, nullptr);
    if (movie_ != nullptr) {
        SDL_BindAudioStream(device_, movie_);
    }
    return true;
}

int SoundSystem::play(const PcmWave& wave, bool looping)
{
    if (device_ == 0) {
        return none;
    }
    // Prefer an idle buffer; otherwise evict the oldest one.
    int handle = none;
    for (int i = 0; i < bufferCount; ++i) {
        const int candidate = (next_ + i) % bufferCount;
        if (!busy(candidate)) {
            handle = candidate;
            break;
        }
    }
    if (handle == none) {
        handle = next_;
    }
    next_ = (handle + 1) % bufferCount;

    auto& buffer = buffers_[static_cast<std::size_t>(handle)];
    const auto spec = specFor(wave);
    SDL_ClearAudioStream(buffer.stream);
    SDL_SetAudioStreamFormat(buffer.stream, &spec, nullptr);
    SDL_SetAudioStreamGain(buffer.stream, 1.0F);
    SDL_SetAudioStreamFrequencyRatio(buffer.stream, 1.0F);
    SDL_PutAudioStreamData(buffer.stream, wave.samples.data(), static_cast<int>(wave.samples.size()));
    if (!looping) {
        // Without a flush the resampler holds back its last few bytes, so
        // the stream would never drain and busy() would stay true.
        SDL_FlushAudioStream(buffer.stream);
    }
    buffer.wave = &wave;
    buffer.looping = looping;
    return handle;
}

void SoundSystem::stop(int handle)
{
    if (handle < 0 || handle >= bufferCount || device_ == 0) {
        return;
    }
    auto& buffer = buffers_[static_cast<std::size_t>(handle)];
    SDL_ClearAudioStream(buffer.stream);
    buffer.wave = nullptr;
    buffer.looping = false;
}

bool SoundSystem::busy(int handle) const
{
    if (handle < 0 || handle >= bufferCount || device_ == 0) {
        return false;
    }
    const auto& buffer = buffers_[static_cast<std::size_t>(handle)];
    return buffer.wave != nullptr && (buffer.looping || SDL_GetAudioStreamQueued(buffer.stream) > 0);
}

// IDirectSoundBuffer::SetVolume: attenuation in 1/100 dB, 0 = full volume.
void SoundSystem::setVolume(int handle, int hundredthsOfDecibel)
{
    if (handle < 0 || handle >= bufferCount || device_ == 0) {
        return;
    }
    // DirectSound takes -10000..0.
    hundredthsOfDecibel = std::min(hundredthsOfDecibel, 0);
    const float gain = hundredthsOfDecibel <= -10000
        ? 0.0F
        : std::pow(10.0F, static_cast<float>(hundredthsOfDecibel) / 2000.0F);
    SDL_SetAudioStreamGain(buffers_[static_cast<std::size_t>(handle)].stream, gain);
}

void SoundSystem::setFrequency(int handle, int hertz)
{
    if (handle < 0 || handle >= bufferCount || device_ == 0) {
        return;
    }
    const auto& buffer = buffers_[static_cast<std::size_t>(handle)];
    if (buffer.wave == nullptr || buffer.wave->sampleRate == 0) {
        return;
    }
    // DirectSound accepts 100..100000 Hz; SDL takes a ratio of 0.01..100.
    const float ratio = static_cast<float>(hertz) / static_cast<float>(buffer.wave->sampleRate);
    SDL_SetAudioStreamFrequencyRatio(buffer.stream, std::clamp(ratio, 0.01F, 100.0F));
}

void SoundSystem::stopAll()
{
    for (int handle = 0; handle < bufferCount; ++handle) {
        stop(handle);
    }
}

void SoundSystem::startStream(int rate, int channels, int bits)
{
    if (movie_ == nullptr) {
        return;
    }
    const SDL_AudioSpec spec{bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8, channels, rate};
    SDL_ClearAudioStream(movie_);
    SDL_SetAudioStreamFormat(movie_, &spec, nullptr);
}

void SoundSystem::queueStream(std::span<const std::uint8_t> samples)
{
    if (movie_ != nullptr && !samples.empty()) {
        SDL_PutAudioStreamData(movie_, samples.data(), static_cast<int>(samples.size()));
    }
}

void SoundSystem::stopStream()
{
    if (movie_ != nullptr) {
        SDL_ClearAudioStream(movie_);
    }
}

void SoundSystem::update()
{
    for (auto& buffer : buffers_) {
        if (buffer.wave == nullptr || !buffer.looping) {
            continue;
        }
        // Queue the next pass while about a second is left, so loops have no gap.
        if (SDL_GetAudioStreamQueued(buffer.stream) < bytesPerSecond(specFor(*buffer.wave))) {
            SDL_PutAudioStreamData(buffer.stream, buffer.wave->samples.data(),
                                   static_cast<int>(buffer.wave->samples.size()));
        }
    }
}

} // namespace opena2::game
