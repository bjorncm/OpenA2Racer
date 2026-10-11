#pragma once

// Road ("weg") of the race: the .TRK centre-line file, the road pieces built
// from it (build_road_elements, 0x41f358) and the render mesh of each road
// segment (build_weg_segment, 0x420431).

#include "opena2/race/race_math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opena2::race {

struct TrackPoint {
    Vec3 position;
    std::uint32_t unused = 0;
    // Tile ids (0..63 of the 8x8 atlas in GRASS2.PCX) of the left verge
    // (lane data A) and the right verge (lane data B).
    std::vector<std::uint32_t> leftTiles;
    std::vector<std::uint32_t> rightTiles;
};

struct Track {
    std::vector<TrackPoint> points;
};

std::optional<Track> decodeTrack(std::span<const std::byte> data, std::string& error);

struct RoadVertex {
    Vec3 position;
    Vec3 normal;
    float u = 0.0F;
    float v = 0.0F;
};

// One road piece (chunk), between centre-line points i and i + 1.
struct RoadPiece {
    // FUN_00422637 direction of the piece in 1024ths (chunk +0x40).
    int heading = 0;
    Vec3 up;          // road plane normal (chunk +0x44)
    float plane = 0;  // plane distance -(up . A) (chunk +0x50)
    Vec3 side;        // segment +0x1c
    Vec3 direction;   // segment +0x28
    // Chunk vertices +0, +4, +8: centre, centre + 120 * side, centre - 120 * side.
    RoadVertex centre;
    RoadVertex right;
    RoadVertex left;
    // Chunk +0x0c..+0x28, in pairs (bottom, top 5 units up): left wall, left
    // face of the central reservation, right wall, right face of the reservation.
    std::array<RoadVertex, 8> walls;
    // Chunk +0x2c / +0x30: verge strips from the left / right road edge outward,
    // 60 units apart.
    std::vector<RoadVertex> leftVerge;
    std::vector<RoadVertex> rightVerge;
};

constexpr float roadHalfWidth = 120.0F;
constexpr float wallHeight = 5.0F;
constexpr float reservationHalfWidth = 5.0F;
constexpr float vergeStep = 60.0F;

// Pass 1 of load_weg_data: one piece per pair of neighbouring points.
std::vector<RoadPiece> buildRoadPieces(const Track& track);

// weg_edge_vectors (0x41eed1): verge normals of the strip between pieces
// a and b, over `laneCount` verge vertices.
void computeVergeNormals(RoadPiece& a, RoadPiece& b, std::size_t laneCount);

// Texture coordinates of corner 0..3 of a tile of the 8x8 atlas (init_tiles,
// 0x425f55).
std::array<std::array<float, 2>, 4> tileCorners(std::uint32_t tile) noexcept;

struct RoadSegmentMesh {
    std::vector<RoadVertex> vertices;
    // Drawn with the road texture (2BAANS.PCX).
    std::vector<std::array<std::uint16_t, 3>> roadTriangles;
    // Drawn with the verge texture (GRASS2.PCX): walls, then verges.
    std::vector<std::array<std::uint16_t, 3>> vergeTriangles;
};

// build_weg_segment: the strip between pieces a and b. `laneCount` verge
// vertices per side are used (the file count, halved at low detail); the
// tiles come from the point at the end of piece a.
RoadSegmentMesh buildRoadSegmentMesh(
    const RoadPiece& a,
    const RoadPiece& b,
    std::size_t laneCount,
    std::span<const std::uint32_t> leftTiles,
    std::span<const std::uint32_t> rightTiles);

} // namespace opena2::race
