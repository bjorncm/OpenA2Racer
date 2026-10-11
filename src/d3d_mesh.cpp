#include "opena2/d3d_mesh.hpp"

#include <bit>
#include <cmath>
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

float readFloat(const std::byte* bytes)
{
    return std::bit_cast<float>(readU32(bytes));
}

} // namespace

D3DMesh decodeD3DMesh(std::span<const std::byte> data, std::string& error)
{
    error.clear();
    constexpr std::uint64_t headerSize = 8;
    constexpr std::uint64_t vertexSize = 33;
    constexpr std::uint64_t faceSize = 9;
    if (data.size() < headerSize) {
        error = "D3D mesh is shorter than its count header.";
        return {};
    }

    const auto vertexCount = readU32(data.data());
    const auto faceCount = readU32(data.data() + 4);
    if (vertexCount == 0 || faceCount == 0 ||
        vertexCount > std::numeric_limits<std::uint16_t>::max()) {
        error = "D3D mesh has unsupported vertex or face counts.";
        return {};
    }

    const std::uint64_t expectedSize = headerSize +
        static_cast<std::uint64_t>(vertexCount) * vertexSize +
        static_cast<std::uint64_t>(faceCount) * faceSize;
    if (expectedSize != data.size()) {
        error = "D3D mesh size does not match its vertex and face counts.";
        return {};
    }

    D3DMesh mesh;
    mesh.vertices.reserve(vertexCount);
    mesh.faces.reserve(faceCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
        const auto* record = data.data() + headerSize +
            static_cast<std::uint64_t>(i) * vertexSize;
        std::array<float, 8> values{};
        for (std::size_t component = 0; component < values.size(); ++component) {
            values[component] = readFloat(record + component * sizeof(float));
            if (!std::isfinite(values[component])) {
                error = "D3D mesh contains a non-finite vertex component.";
                return {};
            }
        }
        mesh.vertices.push_back({
            {values[0], values[1], values[2]},
            {values[3], values[4], values[5]},
            {values[6], values[7]},
            std::to_integer<std::uint8_t>(record[32])});
    }

    const std::uint64_t facesOffset =
        headerSize + static_cast<std::uint64_t>(vertexCount) * vertexSize;
    for (std::uint32_t i = 0; i < faceCount; ++i) {
        const auto* record = data.data() + facesOffset +
            static_cast<std::uint64_t>(i) * faceSize;
        D3DFace face{
            {readU16(record), readU16(record + 2), readU16(record + 4)},
            {
                std::to_integer<std::uint8_t>(record[6]),
                std::to_integer<std::uint8_t>(record[7]),
                std::to_integer<std::uint8_t>(record[8])}};
        for (const auto index : face.indices) {
            if (index >= vertexCount) {
                error = "D3D mesh face references a vertex outside the vertex array.";
                return {};
            }
        }
        mesh.faces.push_back(face);
    }

    return mesh;
}

} // namespace opena2
