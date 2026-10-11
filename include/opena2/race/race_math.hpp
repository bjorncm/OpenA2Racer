#pragma once

// Vector, matrix and angle helpers of the race (A2SPEL.DAT). The original
// uses Direct3D conventions: y is up, the ground is the x/z plane, matrices
// are row-major and points are row vectors (p' = p * M). Angles are in
// 1024ths of a full circle.

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace opena2::race {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3& operator+=(const Vec3& o) noexcept
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr bool operator==(const Vec3&) const = default;
};

constexpr float dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalise(const Vec3& v) noexcept
{
    const float length = std::sqrt(dot(v, v));
    return length == 0.0F ? v : v * (1.0F / length);
}

// Row-major 4x4 matrix, D3D style: m[row][column], translation in row 3.
struct Mat4 {
    std::array<std::array<float, 4>, 4> m{};

    static constexpr Mat4 identity() noexcept
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            r.m[i][i] = 1.0F;
        }
        return r;
    }
    static constexpr Mat4 translation(const Vec3& t) noexcept
    {
        Mat4 r = identity();
        r.m[3] = {t.x, t.y, t.z, 1.0F};
        return r;
    }
};

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[i][k] * b.m[k][j];
            }
            r.m[i][j] = sum;
        }
    }
    return r;
}

constexpr Vec3 transformPoint(const Vec3& p, const Mat4& a) noexcept
{
    return {p.x * a.m[0][0] + p.y * a.m[1][0] + p.z * a.m[2][0] + a.m[3][0],
            p.x * a.m[0][1] + p.y * a.m[1][1] + p.z * a.m[2][1] + a.m[3][1],
            p.x * a.m[0][2] + p.y * a.m[1][2] + p.z * a.m[2][2] + a.m[3][2]};
}

constexpr int angleSteps = 1024;
constexpr int angleMask = angleSteps - 1;

// The sine and cosine tables of alloc_trig_table (0x421af0): 1024 entries.
inline float angleSin(int angle) noexcept
{
    static const auto table = [] {
        std::array<float, angleSteps> t{};
        for (int i = 0; i < angleSteps; ++i) {
            t[static_cast<std::size_t>(i)] = static_cast<float>(
                std::sin(i * 2.0 * std::numbers::pi / angleSteps));
        }
        return t;
    }();
    return table[static_cast<std::size_t>(angle & angleMask)];
}

inline float angleCos(int angle) noexcept
{
    return angleSin(angle + angleSteps / 4);
}

// FUN_00422637: direction from (ax, az) to (bx, bz) in 1024ths, rounded,
// -512..512. Equal x gives +-256 by the sign of the z difference.
inline int headingBetween(float ax, float az, float bx, float bz) noexcept
{
    double angle = 0.0;
    if (ax == bx) {
        angle = az <= bz ? std::numbers::pi / 2.0 : -std::numbers::pi / 2.0;
    } else {
        angle = std::atan2(static_cast<double>(bz) - az, static_cast<double>(bx) - ax);
    }
    return static_cast<int>(std::nearbyint(static_cast<float>(angle) * 1024.0 / 6.283185307));
}

// lcg_rand (0x429085), the C runtime rand: 0..0x7fff. create_main_window
// seeds it with the wall clock in seconds (0x4290a9).
struct Lcg {
    std::uint32_t state = 1;

    int next() noexcept
    {
        state = state * 0x41C64E6DU + 0x3039U;
        return static_cast<int>((state >> 16) & 0x7FFFU);
    }
};

} // namespace opena2::race
