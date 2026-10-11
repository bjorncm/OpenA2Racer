#include "opena2/indexed_image.hpp"

#include <algorithm>
#include <limits>

namespace opena2 {
namespace {

std::uint16_t readU16(const std::byte* bytes)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8);
}

std::int16_t readI16(const std::byte* bytes)
{
    const auto value = readU16(bytes);
    const auto signedValue = value < 0x8000
        ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(value) - 0x10000;
    return static_cast<std::int16_t>(signedValue);
}

} // namespace

std::optional<IndexedImage> decodeIndexedImage(
    std::span<const std::byte> data,
    std::string& error)
{
    error.clear();
    constexpr std::size_t headerSize = 8;
    if (data.size() < headerSize) {
        error = "Indexed image is shorter than its header.";
        return std::nullopt;
    }

    const auto width = readU16(data.data());
    const auto height = readU16(data.data() + 2);
    const auto anchorX = readI16(data.data() + 4);
    const auto anchorY = readI16(data.data() + 6);
    if (width == 0 || height == 0) {
        error = "Indexed image dimensions must be non-zero.";
        return std::nullopt;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount != data.size() - headerSize ||
        pixelCount > std::numeric_limits<std::size_t>::max()) {
        error = "Indexed image dimensions do not match its pixel data length.";
        return std::nullopt;
    }

    IndexedImage image{width, height, anchorX, anchorY, {}, {}};
    image.pixels.reserve(static_cast<std::size_t>(pixelCount));
    for (const auto pixel : data.subspan(headerSize)) {
        image.pixels.push_back(std::to_integer<std::uint8_t>(pixel));
    }
    return image;
}

std::optional<IndexedImage> decodePcx(
    std::span<const std::byte> data,
    std::string& error)
{
    error.clear();
    constexpr std::size_t headerSize = 128;
    constexpr std::size_t paletteSize = 769;
    if (data.size() < headerSize + paletteSize) {
        error = "PCX image is too short to contain a header and palette.";
        return std::nullopt;
    }

    const auto byte = [&data](std::size_t offset) {
        return std::to_integer<std::uint8_t>(data[offset]);
    };
    if (byte(0) != 0x0a || byte(2) != 1 || byte(3) != 8 || byte(65) != 1) {
        error = "PCX image is not an 8-bit, single-plane RLE image.";
        return std::nullopt;
    }

    const auto xMin = readU16(data.data() + 4);
    const auto yMin = readU16(data.data() + 6);
    const auto xMax = readU16(data.data() + 8);
    const auto yMax = readU16(data.data() + 10);
    if (xMax < xMin || yMax < yMin) {
        error = "PCX image has invalid dimensions.";
        return std::nullopt;
    }
    const auto width = static_cast<std::uint32_t>(xMax) - xMin + 1;
    const auto height = static_cast<std::uint32_t>(yMax) - yMin + 1;
    const auto bytesPerLine = readU16(data.data() + 66);
    if (width == 0 || height == 0 || bytesPerLine < width ||
        width > std::numeric_limits<std::uint16_t>::max() ||
        height > std::numeric_limits<std::uint16_t>::max()) {
        error = "PCX image dimensions or row stride are invalid.";
        return std::nullopt;
    }

    const std::size_t paletteStart = data.size() - paletteSize;
    if (byte(paletteStart) != 0x0c) {
        error = "PCX image does not have a trailing 256-color palette.";
        return std::nullopt;
    }

    const std::uint64_t decodedSize = static_cast<std::uint64_t>(height) * bytesPerLine;
    if (decodedSize > std::numeric_limits<std::size_t>::max()) {
        error = "PCX image is too large to decode.";
        return std::nullopt;
    }
    const std::uint64_t compressedSize = paletteStart - headerSize;
    if (decodedSize > compressedSize * 63U) {
        error = "PCX image dimensions cannot be represented by its RLE data.";
        return std::nullopt;
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve(static_cast<std::size_t>(decodedSize));
    std::size_t offset = headerSize;
    while (decoded.size() < decodedSize) {
        if (offset >= paletteStart) {
            error = "PCX RLE data ended before all image rows were decoded.";
            return std::nullopt;
        }

        const auto value = byte(offset++);
        std::size_t runLength = 1;
        std::uint8_t pixelValue = value;
        if ((value & 0xc0) == 0xc0) {
            runLength = value & 0x3f;
            if (runLength == 0 || offset >= paletteStart) {
                error = "PCX image contains an invalid RLE run.";
                return std::nullopt;
            }
            pixelValue = byte(offset++);
        }

        if (runLength > decodedSize - decoded.size()) {
            error = "PCX RLE run exceeds the decoded image size.";
            return std::nullopt;
        }
        decoded.insert(decoded.end(), runLength, pixelValue);
    }

    IndexedImage image{
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        0,
        0,
        {},
        {}};
    image.pixels.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto rowStart = decoded.begin() + static_cast<std::ptrdiff_t>(row) * bytesPerLine;
        image.pixels.insert(
            image.pixels.end(),
            rowStart,
            rowStart + static_cast<std::ptrdiff_t>(width));
    }

    image.paletteRgb.reserve(768);
    for (std::size_t i = paletteStart + 1; i < data.size(); ++i) {
        image.paletteRgb.push_back(byte(i));
    }
    return image;
}

} // namespace opena2
