#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opena2 {

struct D3DVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 2> textureCoordinate;
    std::uint8_t attribute;
};

struct D3DFace {
    std::array<std::uint16_t, 3> indices;
    std::array<std::uint8_t, 3> attributes;
};

struct D3DMesh {
    std::vector<D3DVertex> vertices;
    std::vector<D3DFace> faces;
};

D3DMesh decodeD3DMesh(std::span<const std::byte> data, std::string& error);

} // namespace opena2
