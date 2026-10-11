#include "opena2/pcm_wave.hpp"

#include <limits>

namespace opena2 {
namespace {

std::uint16_t readU16(const std::byte* bytes)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8);
}

std::uint32_t readU32(const std::byte* bytes)
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24);
}

bool hasFourCc(const std::byte* bytes, const char (&value)[5])
{
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::to_integer<unsigned char>(bytes[i]) !=
            static_cast<unsigned char>(value[i])) {
            return false;
        }
    }
    return true;
}

struct FormatChunk {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t byteRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
};

} // namespace

std::optional<PcmWave> decodePcmWave(
    std::span<const std::byte> data,
    std::string& error)
{
    error.clear();
    constexpr std::size_t riffHeaderSize = 12;
    if (data.size() < riffHeaderSize ||
        !hasFourCc(data.data(), "RIFF") ||
        !hasFourCc(data.data() + 8, "WAVE")) {
        error = "Audio resource is not a RIFF/WAVE file.";
        return std::nullopt;
    }

    const std::uint64_t riffEnd = 8ULL + readU32(data.data() + 4);
    if (riffEnd < riffHeaderSize || riffEnd > data.size()) {
        error = "WAVE RIFF size is invalid.";
        return std::nullopt;
    }

    std::optional<FormatChunk> format;
    std::optional<std::span<const std::byte>> samples;
    std::size_t offset = riffHeaderSize;
    while (offset + 8 <= riffEnd) {
        const auto* chunkHeader = data.data() + offset;
        const std::uint32_t chunkSize = readU32(chunkHeader + 4);
        const std::uint64_t chunkDataOffset = offset + 8;
        const std::uint64_t chunkEnd = chunkDataOffset + chunkSize;
        if (chunkEnd > riffEnd) {
            error = "WAVE chunk extends beyond the RIFF container.";
            return std::nullopt;
        }

        const auto* chunkData = data.data() + chunkDataOffset;
        if (hasFourCc(chunkHeader, "fmt ")) {
            if (format || chunkSize < 16) {
                error = "WAVE format chunk is missing or duplicated.";
                return std::nullopt;
            }
            if (readU16(chunkData) != 1) {
                error = "Only uncompressed PCM WAVE files are supported.";
                return std::nullopt;
            }

            format = FormatChunk{
                readU16(chunkData + 2),
                readU32(chunkData + 4),
                readU32(chunkData + 8),
                readU16(chunkData + 12),
                readU16(chunkData + 14)};
        } else if (hasFourCc(chunkHeader, "data")) {
            if (samples) {
                error = "Multiple WAVE data chunks are not supported.";
                return std::nullopt;
            }
            samples = data.subspan(
                static_cast<std::size_t>(chunkDataOffset),
                static_cast<std::size_t>(chunkSize));
        }

        const std::uint64_t nextChunk = chunkEnd + (chunkSize & 1U);
        if (nextChunk > riffEnd) {
            if ((chunkSize & 1U) != 0 && chunkEnd == riffEnd) {
                offset = static_cast<std::size_t>(riffEnd);
                break;
            }
            error = "WAVE chunk padding extends beyond the RIFF container.";
            return std::nullopt;
        }
        offset = static_cast<std::size_t>(nextChunk);
    }

    if (!format || !samples) {
        error = "WAVE file is missing a format or sample-data chunk.";
        return std::nullopt;
    }

    if (format->channels == 0 || format->channels > 2 ||
        format->sampleRate == 0 ||
        (format->bitsPerSample != 8 && format->bitsPerSample != 16)) {
        error = "WAVE PCM channel count, sample rate, or bit depth is unsupported.";
        return std::nullopt;
    }

    const std::uint16_t bytesPerSample = format->bitsPerSample / 8;
    const std::uint32_t expectedBlockAlign =
        static_cast<std::uint32_t>(format->channels) * bytesPerSample;
    if (format->blockAlign != expectedBlockAlign ||
        format->sampleRate > std::numeric_limits<std::uint32_t>::max() / expectedBlockAlign ||
        format->byteRate != format->sampleRate * expectedBlockAlign ||
        samples->size() % format->blockAlign != 0) {
        error = "WAVE PCM byte rate or block alignment does not match its sample data.";
        return std::nullopt;
    }
    PcmWave wave{
        format->sampleRate,
        format->byteRate,
        format->channels,
        format->bitsPerSample == 8
            ? PcmSampleFormat::unsigned8
            : PcmSampleFormat::signed16LittleEndian,
        {}};
    wave.samples.assign(samples->begin(), samples->end());
    return wave;
}

} // namespace opena2
