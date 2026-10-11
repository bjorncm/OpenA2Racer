#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace opena2 {

// Decodes Smacker (.SMK) movies through libsmacker. Frames are 8-bit indexed
// with a per-frame 256-colour palette; audio track 0 is exposed as raw PCM.
class SmackerMovie {
public:
    static std::optional<SmackerMovie> open(const std::filesystem::path& path, std::string& error);

    SmackerMovie(SmackerMovie&&) noexcept;
    SmackerMovie& operator=(SmackerMovie&&) noexcept;
    ~SmackerMovie();

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    // Smacker's Y-doubled and interlaced modes are shown at twice the height.
    int displayHeight() const noexcept { return doubledHeight_ ? height_ * 2 : height_; }
    bool interlaced() const noexcept { return interlaced_; }
    unsigned long frameCount() const noexcept { return frameCount_; }
    double frameSeconds() const noexcept { return frameSeconds_; }

    bool hasAudio() const noexcept { return audioRate_ != 0; }
    unsigned long audioRate() const noexcept { return audioRate_; }
    int audioChannels() const noexcept { return audioChannels_; }
    int audioBits() const noexcept { return audioBits_; }

    // Decodes the first frame. Returns false on a decode error.
    bool rewind();
    // Decodes the next frame. Returns false after the last frame or on error.
    bool next();

    std::span<const std::uint8_t> pixels() const noexcept;
    std::span<const std::uint8_t> paletteRgb() const noexcept;
    // Audio decoded alongside the current frame (unsigned 8-bit or signed
    // 16-bit little-endian PCM, depending on audioBits()).
    std::span<const std::uint8_t> audio() const noexcept;

private:
    struct Handle;

    explicit SmackerMovie(std::unique_ptr<Handle> handle);

    std::unique_ptr<Handle> handle_;
    int width_ = 0;
    int height_ = 0;
    bool doubledHeight_ = false;
    bool interlaced_ = false;
    unsigned long frameCount_ = 0;
    double frameSeconds_ = 0.0;
    unsigned long audioRate_ = 0;
    int audioChannels_ = 0;
    int audioBits_ = 0;
    bool finished_ = false;
};

} // namespace opena2
