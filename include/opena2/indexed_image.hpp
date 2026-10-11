#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opena2 {

struct IndexedImage {
    std::uint16_t width;
    std::uint16_t height;
    std::int16_t anchorX;
    std::int16_t anchorY;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> paletteRgb;
};

std::optional<IndexedImage> decodeIndexedImage(
    std::span<const std::byte> data,
    std::string& error);
std::optional<IndexedImage> decodePcx(
    std::span<const std::byte> data,
    std::string& error);

} // namespace opena2
