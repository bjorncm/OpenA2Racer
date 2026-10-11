#include "opena2/smacker_movie.hpp"

#include <smacker.h>

namespace opena2 {

struct SmackerMovie::Handle {
    smk movie = nullptr;

    ~Handle()
    {
        if (movie != nullptr) {
            smk_close(movie);
        }
    }
};

SmackerMovie::SmackerMovie(std::unique_ptr<Handle> handle)
    : handle_(std::move(handle))
{
}

SmackerMovie::SmackerMovie(SmackerMovie&&) noexcept = default;
SmackerMovie& SmackerMovie::operator=(SmackerMovie&&) noexcept = default;
SmackerMovie::~SmackerMovie() = default;

std::optional<SmackerMovie> SmackerMovie::open(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    auto handle = std::make_unique<Handle>();
    const auto utf8 = path.u8string();
    handle->movie = smk_open_file(reinterpret_cast<const char*>(utf8.c_str()), SMK_MODE_DISK);
    if (handle->movie == nullptr) {
        error = "Could not open the Smacker movie.";
        return std::nullopt;
    }

    unsigned long frame = 0;
    unsigned long frameCount = 0;
    double microsecondsPerFrame = 0.0;
    unsigned long width = 0;
    unsigned long height = 0;
    unsigned char yScale = 0;
    unsigned char trackMask = 0;
    unsigned char channels[7]{};
    unsigned char bits[7]{};
    unsigned long rates[7]{};
    if (smk_info_all(handle->movie, &frame, &frameCount, &microsecondsPerFrame) < 0 ||
        smk_info_video(handle->movie, &width, &height, &yScale) < 0 ||
        smk_info_audio(handle->movie, &trackMask, channels, bits, rates) < 0 ||
        width == 0 || height == 0 || frameCount == 0) {
        error = "The Smacker movie has no usable video.";
        return std::nullopt;
    }

    const bool audio = (trackMask & SMK_AUDIO_TRACK_0) != 0 && rates[0] != 0 &&
                       (bits[0] == 8 || bits[0] == 16) && channels[0] >= 1 && channels[0] <= 2;
    smk_enable_video(handle->movie, 1);
    smk_enable_audio(handle->movie, 0, audio ? 1 : 0);

    SmackerMovie movie(std::move(handle));
    movie.width_ = static_cast<int>(width);
    movie.height_ = static_cast<int>(height);
    movie.doubledHeight_ = yScale == SMK_FLAG_Y_DOUBLE || yScale == SMK_FLAG_Y_INTERLACE;
    movie.interlaced_ = yScale == SMK_FLAG_Y_INTERLACE;
    movie.frameCount_ = frameCount;
    movie.frameSeconds_ = microsecondsPerFrame > 0.0 ? microsecondsPerFrame / 1.0e6 : 0.1;
    if (audio) {
        movie.audioRate_ = rates[0];
        movie.audioChannels_ = channels[0];
        movie.audioBits_ = bits[0];
    }
    if (!movie.rewind()) {
        error = "Could not decode the first Smacker frame.";
        return std::nullopt;
    }
    return movie;
}

bool SmackerMovie::rewind()
{
    finished_ = false;
    const char result = smk_first(handle_->movie);
    if (result < 0) {
        finished_ = true;
        return false;
    }
    return true;
}

bool SmackerMovie::next()
{
    if (finished_) {
        return false;
    }
    unsigned long frame = 0;
    smk_info_all(handle_->movie, &frame, nullptr, nullptr);
    // Ring frames make libsmacker loop; a movie plays once.
    if (frame + 1 >= frameCount_ || smk_next(handle_->movie) < 0) {
        finished_ = true;
        return false;
    }
    return true;
}

std::span<const std::uint8_t> SmackerMovie::pixels() const noexcept
{
    const auto* data = smk_get_video(handle_->movie);
    if (data == nullptr) {
        return {};
    }
    return {data, static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)};
}

std::span<const std::uint8_t> SmackerMovie::paletteRgb() const noexcept
{
    const auto* data = smk_get_palette(handle_->movie);
    if (data == nullptr) {
        return {};
    }
    return {data, 768};
}

std::span<const std::uint8_t> SmackerMovie::audio() const noexcept
{
    if (!hasAudio()) {
        return {};
    }
    const auto* data = smk_get_audio(handle_->movie, 0);
    const auto size = smk_get_audio_size(handle_->movie, 0);
    if (data == nullptr || size == 0) {
        return {};
    }
    return {data, static_cast<std::size_t>(size)};
}

} // namespace opena2
