#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opena2 {

enum class PcmSampleFormat {
    unsigned8,
    signed16LittleEndian
};

struct PcmWave {
    std::uint32_t sampleRate;
    std::uint32_t byteRate;
    std::uint16_t channels;
    PcmSampleFormat format;
    std::vector<std::byte> samples;
};

std::optional<PcmWave> decodePcmWave(
    std::span<const std::byte> data,
    std::string& error);

} // namespace opena2
