#include "opena2/race/track.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace opena2::race {
namespace {

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : data_(data) {}

    bool u32(std::uint32_t& value)
    {
        if (data_.size() - offset_ < 4) {
            return false;
        }
        value = 0;
        for (int i = 3; i >= 0; --i) {
            value = (value << 8) | std::to_integer<std::uint8_t>(data_[offset_ + i]);
        }
        offset_ += 4;
        return true;
    }

    bool f32(float& value)
    {
        std::uint32_t bits = 0;
        if (!u32(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

private:
    std::span<const std::byte> data_;
    std::size_t offset_ = 0;
};

// The lane arrays of the original hold at most 30 dwords.
constexpr std::uint32_t maximumLaneCount = 30;

RoadVertex vertexAt(const Vec3& position, const Vec3& normal, float u, float v)
{
    return {position, normal, u, v};
}

} // namespace

std::optional<Track> decodeTrack(std::span<const std::byte> data, std::string& error)
{
    error.clear();
    Reader reader(data);
    std::uint32_t count = 0;
    if (!reader.u32(count) || count < 2) {
        error = "Track file has no centre line.";
        return std::nullopt;
    }
    Track track;
    track.points.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TrackPoint point;
        std::uint32_t y = 0;
        std::uint32_t lanes = 0;
        if (!reader.f32(point.position.x) || !reader.f32(point.position.z) ||
            !reader.u32(point.unused) || !reader.u32(y) || !reader.u32(lanes)) {
            error = "Track file ends inside a point record.";
            return std::nullopt;
        }
        // The height is stored as an integer and converted to float.
        point.position.y = static_cast<float>(static_cast<std::int32_t>(y));
        if (lanes < 2 || lanes > maximumLaneCount) {
            error = "Track point has an unsupported lane count.";
            return std::nullopt;
        }
        point.leftTiles.resize(lanes);
        point.rightTiles.resize(lanes);
        for (auto* tiles : {&point.leftTiles, &point.rightTiles}) {
            for (auto& tile : *tiles) {
                if (!reader.u32(tile)) {
                    error = "Track file ends inside a lane array.";
                    return std::nullopt;
                }
            }
        }
        track.points.push_back(std::move(point));
    }
    return track;
}

std::vector<RoadPiece> buildRoadPieces(const Track& track)
{
    std::vector<RoadPiece> pieces;
    if (track.points.size() < 2) {
        return pieces;
    }
    pieces.reserve(track.points.size() - 1);
    for (std::size_t i = 0; i + 1 < track.points.size(); ++i) {
        const Vec3 a = track.points[i].position;
        const Vec3 b = track.points[i + 1].position;
        // The lane count read with point B (load_weg_data passes it on).
        const std::size_t lanes = track.points[i + 1].leftTiles.size();

        RoadPiece piece;
        piece.heading = headingBetween(a.x, a.z, b.x, b.z);
        const Vec3 dir = normalise(b - a);
        // (0, 1, 0) minus its component along the road direction.
        piece.up = normalise(Vec3{0.0F, 1.0F, 0.0F} - dir * dir.y);
        const Vec3 side = normalise(cross(piece.up, dir));
        piece.plane = -dot(piece.up, a);
        if (piece.up.y == 0.0F) {
            piece.up.y = 0.1F; // "error nul b"
        }
        piece.side = side;
        piece.direction = dir;

        const Vec3 edge = side * roadHalfWidth;
        piece.centre = vertexAt(a, piece.up, 0.0F, 0.0F);
        piece.right = vertexAt({a.x + edge.x, a.y, a.z + edge.z}, piece.up, 1.0F, 0.0F);
        piece.left = vertexAt({a.x - edge.x, a.y, a.z - edge.z}, piece.up, 1.0F, 0.0F);

        const Vec3 lift = piece.up * wallHeight;
        const Vec3 reservation = side * reservationHalfWidth;
        const Vec3 innerLeft{a.x - reservation.x, a.y, a.z - reservation.z};
        const Vec3 innerRight{a.x + reservation.x, a.y, a.z + reservation.z};
        const std::array<Vec3, 8> walls{
            piece.left.position, piece.left.position + lift,
            innerLeft, innerLeft + lift,
            piece.right.position, piece.right.position + lift,
            innerRight, innerRight + lift,
        };
        for (std::size_t w = 0; w < walls.size(); ++w) {
            piece.walls[w] = vertexAt(walls[w], side, 0.0F, 0.0F);
        }

        const Vec3 step = side * vergeStep;
        Vec3 left = piece.left.position;
        Vec3 right = piece.right.position;
        for (std::size_t k = 0; k < lanes; ++k) {
            // Only the first verge vertex keeps the road height.
            const float height = k == 0 ? a.y : 0.0F;
            piece.leftVerge.push_back(
                vertexAt({left.x, height, left.z}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F));
            piece.rightVerge.push_back(
                vertexAt({right.x, height, right.z}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F));
            left = left - Vec3{step.x, 0.0F, step.z};
            right = right + Vec3{step.x, 0.0F, step.z};
        }
        pieces.push_back(std::move(piece));
    }
    return pieces;
}

void computeVergeNormals(RoadPiece& a, RoadPiece& b, std::size_t laneCount)
{
    auto normalAt = [](const Vec3& from, const Vec3& to1, const Vec3& to2) {
        // get_vertex gives to - from; cross_3d(out, p, q) = p x q.
        const Vec3 e1 = normalise(to1 - from);
        const Vec3 e2 = normalise(to2 - from);
        Vec3 n = cross(e2, e1);
        n.y = std::abs(n.y);
        return n;
    };
    // Right verge first (+0x30), then the left one (+0x2c).
    for (auto strip : {&RoadPiece::rightVerge, &RoadPiece::leftVerge}) {
        auto& near = a.*strip;
        auto& far = b.*strip;
        const std::size_t count = std::min({laneCount, near.size(), far.size()});
        for (std::size_t k = 0; k + 1 < count; ++k) {
            const Vec3 pa = near[k].position;
            const Vec3 pb = near[k + 1].position;
            const Vec3 pc = far[k].position;
            const Vec3 pd = far[k + 1].position;
            near[k].normal = normalAt(pa, pb, pc);
            near[k + 1].normal = normalAt(pb, pa, pd);
            // Corner c is written twice; the second write (from corner d) stays.
            far[k].normal = normalAt(pd, pb, pc);
        }
    }
}

std::array<std::array<float, 2>, 4> tileCorners(std::uint32_t tile) noexcept
{
    constexpr float size = 0.125F;
    const float column = static_cast<float>(tile % 8) * size;
    const float row = static_cast<float>((tile / 8) % 8) * size;
    return {{{column, row + size},
             {column, row},
             {column + size, row},
             {column + size, row + size}}};
}

RoadSegmentMesh buildRoadSegmentMesh(
    const RoadPiece& a,
    const RoadPiece& b,
    std::size_t laneCount,
    std::span<const std::uint32_t> leftTiles,
    std::span<const std::uint32_t> rightTiles)
{
    RoadSegmentMesh mesh;
    auto add = [&mesh](const RoadVertex& source, float u, float v) {
        RoadVertex vertex = source;
        vertex.u = u;
        vertex.v = v;
        mesh.vertices.push_back(vertex);
    };

    // Vertices 0..7: the right and the left half of the road surface.
    add(a.centre, 0.0F, 0.0F);
    add(a.right, 1.0F, 0.0F);
    add(b.centre, 0.0F, 1.0F);
    add(b.right, 1.0F, 1.0F);
    add(a.centre, 0.0F, 0.0F);
    add(a.left, 1.0F, 0.0F);
    add(b.centre, 0.0F, 1.0F);
    add(b.left, 1.0F, 1.0F);
    mesh.roadTriangles = {{0, 2, 3}, {0, 3, 1}, {4, 7, 6}, {4, 5, 7}};

    // Vertices 8..23: four wall quads. The kerb texture is the top tile row:
    // tile 0 corners 0 and 1 on piece a, tile 7 corners 3 and 2 on piece b.
    const auto first = tileCorners(0);
    const auto last = tileCorners(7);
    for (std::size_t wall = 0; wall < 4; ++wall) {
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        add(a.walls[wall * 2], first[0][0], first[0][1]);
        add(a.walls[wall * 2 + 1], first[1][0], first[1][1]);
        add(b.walls[wall * 2], last[3][0], last[3][1]);
        add(b.walls[wall * 2 + 1], last[2][0], last[2][1]);
        // Both sides of each wall are drawn.
        const auto s = base;
        mesh.vergeTriangles.push_back({s, static_cast<std::uint16_t>(s + 2), static_cast<std::uint16_t>(s + 3)});
        mesh.vergeTriangles.push_back({s, static_cast<std::uint16_t>(s + 3), static_cast<std::uint16_t>(s + 1)});
        mesh.vergeTriangles.push_back({static_cast<std::uint16_t>(s + 3), static_cast<std::uint16_t>(s + 2), s});
        mesh.vergeTriangles.push_back({static_cast<std::uint16_t>(s + 1), static_cast<std::uint16_t>(s + 3), s});
    }

    auto quad = [&mesh](std::uint16_t s) {
        mesh.vergeTriangles.push_back({s, static_cast<std::uint16_t>(s + 2), static_cast<std::uint16_t>(s + 3)});
        mesh.vergeTriangles.push_back({s, static_cast<std::uint16_t>(s + 3), static_cast<std::uint16_t>(s + 1)});
    };
    const std::size_t count = std::min({laneCount, a.leftVerge.size(), b.leftVerge.size(),
                                        a.rightVerge.size(), b.rightVerge.size()});
    // Left verge: outer vertex first.
    for (std::size_t k = 0; k + 1 < count; ++k) {
        const auto corners = tileCorners(k < leftTiles.size() ? leftTiles[k] : 0);
        const auto s = static_cast<std::uint16_t>(mesh.vertices.size());
        add(a.leftVerge[k + 1], corners[0][0], corners[0][1]);
        add(a.leftVerge[k], corners[1][0], corners[1][1]);
        add(b.leftVerge[k + 1], corners[3][0], corners[3][1]);
        add(b.leftVerge[k], corners[2][0], corners[2][1]);
        quad(s);
    }
    // Right verge: inner vertex first.
    for (std::size_t k = 0; k + 1 < count; ++k) {
        const auto corners = tileCorners(k < rightTiles.size() ? rightTiles[k] : 0);
        const auto s = static_cast<std::uint16_t>(mesh.vertices.size());
        add(a.rightVerge[k], corners[0][0], corners[0][1]);
        add(a.rightVerge[k + 1], corners[1][0], corners[1][1]);
        add(b.rightVerge[k], corners[3][0], corners[3][1]);
        add(b.rightVerge[k + 1], corners[2][0], corners[2][1]);
        quad(s);
    }
    return mesh;
}

} // namespace opena2::race
