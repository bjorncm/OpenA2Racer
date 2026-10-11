#include "opena2/race/collision.hpp"

#include <algorithm>
#include <cmath>

namespace opena2::race {
namespace {

// Constants of the car step and the walls.
constexpr float wallOuter = 120.0F;
constexpr float wallInner = 5.0F;
constexpr float endNear = 1.0F;
constexpr float endFar = 59.0F;
constexpr float wallSpeedFactor = 0.845F;
constexpr float bumpSpeed = 4.0F;

// The bounce (0x443ba4..0x443bcc).
constexpr float bounceShare = 0.6F;
constexpr float bounceLimit = 10.0F;
constexpr float crashSpeedFactor = 0.7F;
constexpr float crashBounceFactor = 0.8F;

// FUN_0040bcaf: the 2-D cross product of an edge and a vector.
float side(const Vec3& edge, const Vec3& v) noexcept
{
    return edge.x * v.z - edge.z * v.x;
}

// Four side() tests against the edges q0->q1, q1->q2, q2->q3, q3->q0.
bool inside(const std::array<Vec3, 5>& quad, const Vec3& p) noexcept
{
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec3& from = quad[i];
        const Vec3& to = quad[(i + 1) % 4];
        if (side(to - from, p - from) < 0.0F) {
            return false;
        }
    }
    return true;
}

// FUN_00409cfc: both cars get the reflected mean velocity as bounce; then
// `a`'s step is shortened until the cars no longer touch.
void bounce(CarBody& a, CarBody& b, std::vector<Crash>* crashes)
{
    const Vec3 va = a.step;
    const Vec3 vb = b.step;
    const float mass = a.mass + b.mass;
    const float cx = (b.mass * vb.x + a.mass * va.x) / mass;
    const float cz = (b.mass * vb.z + a.mass * va.z) / mass;
    const auto push = [](CarBody& car, float dx, float dz) {
        car.bounce.x = dx * bounceShare + car.bounce.x;
        car.bounce.z = dz * bounceShare + car.bounce.z;
        if (bounceLimit < std::abs(car.bounce.x) || bounceLimit < std::abs(car.bounce.z)) {
            car.bounce.x -= dx * bounceShare;
            car.bounce.z -= dz * bounceShare;
        }
    };
    push(a, (cx - va.x) + cx, (cz - va.z) + cz);
    push(b, (cx - vb.x) + cx, (cz - vb.z) + cz);

    if (collideCars(a, b, false) == contact::none) {
        return;
    }
    // The test also counts a near contact, so this mostly stops at 0.1.
    float t = 0.1F;
    for (; t < 1.0F; t += 0.1F) {
        a.step.x = t * va.x;
        a.step.z = t * va.z;
        if (collideCars(a, b, false) != contact::none) {
            break;
        }
    }
    a.step.x = (t + -0.1F) * va.x;
    a.step.z = (t + -0.1F) * va.z;
    a.speed *= crashSpeedFactor;
    a.bounce.x *= crashBounceFactor;
    a.bounce.z *= crashBounceFactor;
    if (crashes != nullptr) {
        crashes->push_back({a.position, a.speed});
    }
}

} // namespace

CarBox CarBox::fromVertices(std::span<const Vec3> vertices) noexcept
{
    float minX = 10000.0F;
    float minZ = 10000.0F;
    float maxX = 0.0F;
    float maxZ = 0.0F;
    for (const auto& v : vertices) {
        minX = std::min(minX, v.x);
        minZ = std::min(minZ, v.z);
        maxX = std::max(maxX, v.x);
        maxZ = std::max(maxZ, v.z);
    }
    CarBox box;
    box.corners = {Vec3{minX, 0.0F, minZ}, Vec3{maxX, 0.0F, minZ}, Vec3{maxX, 0.0F, maxZ},
                   Vec3{minX, 0.0F, maxZ}};
    box.radiusSquared = maxZ * maxZ + maxX * maxX;
    return box;
}

void CarBody::updateCorners() noexcept
{
    const int h = heading & angleMask;
    const float s = angleSin(h);
    const float c = angleCos(h);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec3& l = box.corners[i];
        corners[i] = {l.x * c - l.z * s + position.x, 0.0F, l.z * c + l.x * s + position.z};
    }
}

void CarBody::startStep() noexcept
{
    const int h = (heading + 0x100) & angleMask;
    direction = {angleCos(h), 0.0F, angleSin(h)};
    step = {direction.x * speed + bounce.x, 0.0F, direction.z * speed + bounce.z};
}

bool hitRoadWalls(std::span<const RoadPiece> pieces, CarBody& car, bool& bump)
{
    if (car.piece < 0 || car.piece >= static_cast<int>(pieces.size())) {
        return false;
    }
    const RoadPiece& road = pieces[static_cast<std::size_t>(car.piece)];
    const int h = road.heading & angleMask;
    const float c = angleCos(h);
    const float s = angleSin(h);
    float across = c * car.step.z + -s * car.step.x;
    if (across == 0.0F) {
        return false;
    }
    if (car.forward) {
        across = -across;
    }
    float limit = across;
    bool hit = false;
    for (const auto& corner : car.corners) {
        const float dx = corner.x - road.centre.position.x;
        const float dz = corner.z - road.centre.position.z;
        const float distance = std::abs(-s * dx + c * dz);
        if (wallOuter < distance + limit) {
            hit = true;
            limit = wallOuter - distance;
        }
        if (distance + limit < wallInner) {
            hit = true;
            limit = wallInner - distance;
        }
    }
    if (!hit) {
        return false;
    }
    car.step.x *= limit / across;
    car.step.z = limit / across * car.step.z;
    car.speed *= wallSpeedFactor;
    if (bumpSpeed < car.speed) {
        bump = true;
    }
    return true;
}

bool hitTrackEnds(std::span<const RoadPiece> pieces, CarBody& car, bool& bump)
{
    const int count = static_cast<int>(pieces.size());
    if (car.piece >= 6 && car.piece < count - 6) {
        return false;
    }
    if (car.piece < 0 || car.piece >= count) {
        return false;
    }
    const RoadPiece& road = pieces[static_cast<std::size_t>(car.piece)];
    const int h = road.heading & angleMask;
    const float c = angleCos(h);
    const float s = angleSin(h);
    const float along = s * car.step.z + c * car.step.x;
    if (along == 0.0F) {
        return false;
    }
    float limit = along;
    bool hit = false;
    for (const auto& corner : car.corners) {
        const float dx = corner.x - road.centre.position.x;
        const float dz = corner.z - road.centre.position.z;
        const float distance = std::abs(c * dx + s * dz);
        if (car.forward) {
            if (distance + limit < endNear) {
                hit = true;
                limit = endNear - distance;
            }
        } else if (endFar < distance + limit) {
            hit = true;
            limit = endFar - distance;
        }
    }
    if (!hit) {
        return false;
    }
    car.step.x *= limit / along;
    car.step.z = limit / along * car.step.z;
    car.speed *= wallSpeedFactor;
    if (bumpSpeed < car.speed) {
        bump = true;
    }
    return true;
}

int collideCars(CarBody& a, CarBody& b, bool apply, std::vector<Crash>* crashes)
{
    const float movedX = a.position.x + a.step.x;
    const float movedZ = a.position.z + a.step.z;
    const float dx = b.position.x - movedX;
    const float dz = b.position.z - movedZ;
    const float reach = a.box.radiusSquared + b.box.radiusSquared;
    if (dz * dz + dx * dx > reach * reach) {
        return contact::none;
    }
    int result = contact::nearby;

    // Car a's corners moved by its step, then its moved centre; car b's
    // corners and centre where it stands. Copies: a bounce changes the step,
    // not these points.
    std::array<Vec3, 5> pointsA;
    std::array<Vec3, 5> pointsB;
    for (std::size_t i = 0; i < 4; ++i) {
        pointsA[i] = {a.corners[i].x + a.step.x, 0.0F, a.corners[i].z + a.step.z};
        pointsB[i] = {b.corners[i].x, 0.0F, b.corners[i].z};
    }
    pointsA[4] = {movedX, 0.0F, movedZ};
    pointsB[4] = {b.position.x, 0.0F, b.position.z};

    const auto test = [&](const std::array<Vec3, 5>& quad, const std::array<Vec3, 5>& points) {
        for (const auto& p : points) {
            if (!inside(quad, p)) {
                continue;
            }
            result = contact::hit;
            if (!apply) {
                return true;
            }
            bounce(a, b, crashes);
        }
        return false;
    };
    if (test(pointsA, pointsB)) {
        return contact::hit;
    }
    if (result != contact::hit && test(pointsB, pointsA)) {
        return contact::hit;
    }
    return result;
}

} // namespace opena2::race
