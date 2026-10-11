#include "opena2/race/hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace opena2::race {
namespace {

// Frame lists of DAT_00458a54 (count, pointer to 8-byte frame records whose
// dword at +4 is the image number), read from the file.
constexpr std::array<int, 1> anim0{1};
constexpr std::array<int, 40> anim1{56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                    70, 71, 72, 73, 74, 75, 217, 218, 219, 220, 221, 222, 223, 224,
                                    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236};
constexpr std::array<int, 20> anim2{91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
                                    101, 102, 103, 104, 105, 106, 107, 108, 109, 110};
constexpr std::array<int, 20> anim3{249, 250, 251, 252, 253, 254, 255, 256, 257, 258,
                                    259, 260, 261, 262, 263, 264, 265, 266, 267, 268};
constexpr std::array<int, 15> anim4{76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90};
constexpr std::array<int, 40> anim5{4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18,
                                    19, 111, 112, 113, 114, 115, 157, 158, 159, 160, 161, 162, 163, 164,
                                    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176};
constexpr std::array<int, 40> anim6{116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
                                    130, 131, 132, 133, 134, 135, 197, 198, 199, 200, 201, 202, 203, 204,
                                    205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216};
constexpr std::array<int, 20> anim7{136, 138, 139, 140, 141, 142, 143, 144, 145, 146,
                                    147, 148, 149, 150, 151, 152, 153, 154, 155, 156};
constexpr std::array<int, 20> anim8{177, 178, 179, 180, 181, 182, 183, 184, 185, 186,
                                    187, 188, 189, 190, 191, 192, 193, 194, 195, 196};
constexpr std::array<int, 6> anim9{137, 243, 244, 245, 246, 247};
constexpr std::array<int, 6> anim10{248, 237, 238, 239, 240, 241};
constexpr std::array<int, 1> anim11{12};
constexpr std::array<int, 1> anim12{242};
constexpr std::array<int, 1> anim13{21};
constexpr std::array<int, 10> anim14{31, 22, 23, 24, 25, 26, 27, 28, 29, 30};
constexpr std::array<int, 1> anim15{42};
constexpr std::array<int, 10> anim16{32, 33, 34, 35, 36, 37, 38, 39, 40, 41};
constexpr std::array<int, 5> anim17{48, 49, 50, 51, 43};
constexpr std::array<int, 5> anim18{52, 44, 45, 46, 47};
constexpr std::array<int, 16> anim19{299, 285, 286, 287, 288, 289, 290, 291,
                                     292, 293, 294, 295, 296, 297, 298, 284};
constexpr std::array<int, 16> anim20{283, 269, 270, 271, 272, 273, 274, 275,
                                     276, 277, 278, 279, 280, 281, 282, 53};
constexpr std::array<int, 1> anim21{300};
constexpr std::array<int, 1> anim22{302};
constexpr std::array<int, 1> anim23{324};
constexpr std::array<int, 1> anim24{323};
constexpr std::array<int, 1> anim25{331};
constexpr std::array<int, 1> anim26{332};
constexpr std::array<int, 10> anim27{312, 313, 314, 315, 316, 317, 318, 319, 320, 303};
constexpr std::array<int, 10> anim28{54, 301, 304, 305, 306, 307, 308, 309, 310, 311};
constexpr std::array<int, 1> anim29{326};
constexpr std::array<int, 1> anim30{325};
constexpr std::array<int, 1> anim31{328};
constexpr std::array<int, 1> anim32{329};
constexpr std::array<int, 1> anim33{327};
constexpr std::array<int, 1> anim34{330};

constexpr std::array<std::span<const int>, hudAnimationCount> animations{
    anim0, anim1, anim2, anim3, anim4, anim5, anim6, anim7, anim8, anim9, anim10, anim11,
    anim12, anim13, anim14, anim15, anim16, anim17, anim18, anim19, anim20, anim21, anim22, anim23,
    anim24, anim25, anim26, anim27, anim28, anim29, anim30, anim31, anim32, anim33, anim34};

// The animation entries draw_hud uses in the high-resolution layout.
namespace entry {
constexpr int frame = 0;
constexpr int speedometer = 1;
constexpr int tachometer = 2;
constexpr int fuel = 3;
constexpr int wheel = 4;
constexpr int simpleSpeedometer = 6;
constexpr int simpleFuel = 8;
constexpr int place = 10;
constexpr int placeLabel = 12;
constexpr int clock = 13;
constexpr int digits = 14;
constexpr int moneyLabel = 29;
constexpr int startLights = 18;
constexpr int finishFlag = 19;
constexpr int fineBox = 22;
constexpr int moneyOut = 24;
constexpr int paused = 26;
constexpr int fineDigits = 28;
constexpr int timeOut = 32;
constexpr int fuelOut = 34;
} // namespace entry

int frameImage(int animation, int frame)
{
    const auto list = animations[static_cast<std::size_t>(animation)];
    return list[static_cast<std::size_t>(std::clamp(frame, 0, static_cast<int>(list.size()) - 1))];
}

// The gauge scales, doubles at 0x443b79..0x443b99.
constexpr double speedScale = 8.0 / 3.0;
constexpr double tachometerScale = 2.0 / 7.0;
constexpr double fuelScale = 1.0 / 15.0;

} // namespace

std::span<const int> hudAnimation(int entry) noexcept
{
    if (entry < 0 || entry >= hudAnimationCount) {
        return {};
    }
    return animations[static_cast<std::size_t>(entry)];
}

bool hudDashboard(const Infio& infio) noexcept
{
    return infio.dashboard == 1 && infio.displayMode != 1;
}

int gaugeFrame(double value, double scale, int last) noexcept
{
    if (value == 0.0) {
        return 0;
    }
    // ROUND is the x87 round-to-nearest-even.
    const int frame = std::abs(static_cast<int>(std::nearbyint(value * scale)));
    return std::min(frame, last);
}

std::vector<HudBlit> hudBlits(bool dashboard, const HudValues& values)
{
    std::vector<HudBlit> blits;
    // Blit mode 2 (top-left) unless noted; mode 0 places the origin.
    const auto blit = [&blits](int animation, int frame, int x, int y, bool atOrigin = false) {
        blits.push_back({frameImage(animation, frame), x, y, atOrigin});
    };

    if (!dashboard) {
        // FUN_00409275 and fuel_gauge_value.
        blit(entry::simpleSpeedometer, gaugeFrame(values.speed, speedScale, 39), 0x1e4, 0x166);
        blit(entry::simpleFuel, gaugeFrame(values.fuel, fuelScale, 19), 0x1b0, 0x193);
    } else {
        // draw_hud_frame, draw_frame_counter, draw_fuel_gauge,
        // FUN_004099be and FUN_00409a53.
        blit(entry::frame, 0, 0, 0x10b);
        blit(entry::wheel, values.steerIndex / 2, 0x2c, 0x15c);
        blit(entry::fuel, gaugeFrame(values.fuel, fuelScale, 19), 0x124, 0x148);
        blit(entry::speedometer, gaugeFrame(values.speed, speedScale, 39), 0xd7, 0x140);
        blit(entry::tachometer, gaugeFrame(values.soundIndex, tachometerScale, 19), 0x87, 0x13f);
    }

    // FUN_00409383: the place, then its label.
    blit(entry::place, std::clamp(values.place - 1, 0, 5), 0x24e, 0x19, true);
    blit(entry::placeLabel, 0, 0x1fc, 0xf);

    // draw_race_clock: hours, minutes and seconds, two digits each.
    const std::int32_t t = values.clock;
    const std::int32_t hours = t / 3600;
    const std::int32_t minutes = (t / 60) % 60;
    const std::int32_t seconds = t % 60;
    blit(entry::clock, 0, 0x10, 0x10);
    constexpr std::array<int, 6> clockX{0, 0x14, 0x39, 0x4e, 0x71, 0x86};
    const std::array<std::int32_t, 6> clockDigits{hours / 10, hours % 10, minutes / 10,
                                                  minutes % 10, seconds / 10, seconds % 10};
    for (std::size_t i = 0; i < clockX.size(); ++i) {
        blit(entry::digits, clockDigits[i], 0x42 + clockX[i], 0x18, true);
    }

    // draw_money_hud: the label, then four digits.
    const std::int32_t money = values.money;
    blit(entry::moneyLabel, 0, 0x10, 0x2e);
    const std::array<std::int32_t, 4> moneyDigits{money / 1000, (money % 1000) / 100,
                                                  (money % 100) / 10, money % 10};
    for (std::size_t i = 0; i < moneyDigits.size(); ++i) {
        blit(entry::digits, moneyDigits[i], 0x42 + 0x14 * static_cast<int>(i), 0x36, true);
    }
    return blits;
}

std::vector<HudBlit> stateBlits(bool dashboard, int state, int startStep, std::int32_t fine,
                                int finishFrame)
{
    std::vector<HudBlit> blits;
    const auto blit = [&blits](int animation, int frame, int x, int y, bool atOrigin) {
        blits.push_back({frameImage(animation, frame), x, y, atOrigin});
    };
    // The messages sit higher with the dashboard, in the smaller view.
    switch (state) {
    case 2:
        blit(entry::startLights, startStep, 0x96, dashboard ? 0xa0 : 200, false);
        break;
    case 3:
        blit(entry::finishFlag, finishFrame, 0x140, dashboard ? 0xa0 : 200, true);
        break;
    case 4: {
        // The fine: the box, then up to four digits (no leading thousands
        // digit when it is 0).
        blit(entry::fineBox, 0, 0xa0, dashboard ? 0x3c : 100, false);
        const int y = dashboard ? 0xbe : 0xe6;
        if (fine / 1000 != 0) {
            blit(entry::fineDigits, fine / 1000, 0x186, y, true);
        }
        blit(entry::fineDigits, (fine % 1000) / 100, 0x186 + 0xc, y, true);
        blit(entry::fineDigits, (fine % 100) / 10, 0x186 + 0x18, y, true);
        blit(entry::fineDigits, fine % 10, 0x186 + 0x24, y, true);
        break;
    }
    case 5:
        blit(entry::timeOut, 0, 0x140, dashboard ? 0x8c : 0xf0, true);
        break;
    case 6:
        blit(entry::moneyOut, 0, 0xa0, dashboard ? 0x3c : 100, false);
        break;
    case 7:
        blit(entry::fuelOut, 0, 0x140, dashboard ? 0x8c : 0xf0, true);
        break;
    case 9:
        blit(entry::paused, 0, 0xa0, dashboard ? 100 : 0x8c, false);
        break;
    default:
        break;
    }
    return blits;
}

RadarLayout radarLayout(bool dashboard) noexcept
{
    if (dashboard) {
        return {0x1e6, 0x179, 0x3a, 0x3a, 0x1d, 0x1d, false, false};
    }
    return {6, 0x166, 0x74, 0x74, 0x38, 0x38, true, true};
}

std::vector<std::uint8_t> radarPixels(const RadarLayout& layout, const Vec3& player, int heading,
                                      std::span<const RadarCar> cars)
{
    const auto width = static_cast<std::size_t>(layout.width);
    std::vector<std::uint8_t> pixels(width * static_cast<std::size_t>(layout.height), 0);

    // FUN_0040f8f3: even rows start with the colour key, odd rows with black.
    if (layout.dithered) {
        for (int y = 0; y < layout.height; ++y) {
            for (int x = y % 2; x < layout.width; x += 2) {
                pixels[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = radarTransparent;
            }
        }
    }

    const auto dot = [&](int x, int y, std::uint8_t colour) {
        const int size = layout.bigDots ? 4 : 2;
        for (int dy = 0; dy < size; ++dy) {
            for (int dx = 0; dx < size; ++dx) {
                const int px = x + dx;
                const int py = y + dy;
                if (px >= 0 && py >= 0 && px < layout.width && py < layout.height) {
                    pixels[static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px)] = colour;
                }
            }
        }
    };

    dot(layout.centreX, layout.centreY, 0x14);
    const int angle = -heading & angleMask;
    const float c = angleCos(angle);
    const float s = angleSin(angle);
    for (const auto& car : cars) {
        constexpr float scale = 12.0F;
        const float dx = (car.position.x - player.x) / scale;
        const float dz = (car.position.z - player.z) / scale;
        const int x = static_cast<int>(std::nearbyint(static_cast<float>(layout.centreX) + (dx * c - dz * s)));
        const int y = static_cast<int>(std::nearbyint(
            static_cast<float>(layout.height) - (static_cast<float>(layout.centreY) + dz * c + dx * s)));
        std::uint8_t colour = 0x0a;
        if (car.type == 3) {
            colour = 0x3c;
        } else if (car.type == 4) {
            colour = 0x58;
        }
        if (x > 0 && x < layout.width - 4 && y > 0 && y < layout.height - 4) {
            dot(x, y, colour);
        }
    }
    return pixels;
}

} // namespace opena2::race
