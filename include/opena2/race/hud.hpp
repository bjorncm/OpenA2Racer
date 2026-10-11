#pragma once

// The race HUD of A2SPEL.DAT (dashbrd.cpp, radar.cpp): which GAMESPR images
// draw_hud (0x4091f1) blits where, and the radar picture of
// draw_radar_pointer (0x40fdb4).
//
// Only the high-resolution layouts (DAT_00458c28 = 0) are ported; the
// low-resolution layouts belong to the 320x240 display modes.

#include "opena2/menu_data.hpp"
#include "opena2/race/race_math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace opena2::race {

// The 35 frame lists of the HUD animation table (DAT_00458a54, turned into
// DAT_004770e8 by FUN_00429ecc). Each frame is a GAMESPR image number,
// counted from 1.
constexpr int hudAnimationCount = 35;
std::span<const int> hudAnimation(int entry) noexcept;

// load_a2_ini (0x40c2f4): DASHBORD 1 shows the dashboard (DAT_00458c20);
// DISPLAY_MODE 1 (low resolution) turns it off.
bool hudDashboard(const Infio& infio) noexcept;

// The 3D viewport for the original's display configurations:
// 640x300 with the dashboard, else 640x480.
constexpr int viewportHeight(bool dashboard) noexcept { return dashboard ? 300 : 480; }

// What the gauges show, from the globals draw_hud reads.
struct HudValues {
    // DAT_0045b260: the player's speed in units per frame.
    float speed = 0.0F;
    // DAT_0045b2ac: the engine sound index of the speed record.
    int soundIndex = 0;
    // DAT_00457748: the steer index 0..28 the wheel turns to.
    int steerIndex = 14;
    // DAT_00458c4c, DAT_00458c54, DAT_00458c5c.
    std::int32_t fuel = 0;
    std::int32_t clock = 0;
    std::int32_t money = 0;
    // racer_place() of the player, 1..6.
    int place = 1;
};

// One blit_surface call (0x42b674): a GAMESPR image at (x, y). Without
// blit flag 2 (`atOrigin`) the image's origin (its header's x and y origin)
// goes to (x, y); with it, its top-left.
struct HudBlit {
    int image = 0;
    int x = 0;
    int y = 0;
    bool atOrigin = false;
};

// The gauge frame: |round(value * scale)|, at most `last`; 0 for 0.
int gaugeFrame(double value, double scale, int last) noexcept;

// draw_hud (0x4091f1) in call order.
std::vector<HudBlit> hudBlits(bool dashboard, const HudValues& values);

// What state_dispatch (0x40c814) draws over the frame after draw_hud: the
// start lights (draw_hud_element, 0x411601) for start-light step
// `startStep`, the police fine `fine`, the time, money and fuel out
// messages, the pause sign, and frame `finishFrame` 0..15 of the finish
// animation (stage_hud_element, 0x41166b).
std::vector<HudBlit> stateBlits(bool dashboard, int state, int startStep, std::int32_t fine,
                                int finishFrame);

// radar_layout (0x40f3ad) for the high-resolution display.
struct RadarLayout {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int centreX = 0;
    int centreY = 0;
    // DAT_00475170: 4x4 dots instead of 2x2.
    bool bigDots = false;
    // Without the dashboard the background is a 50% dither of black and
    // the transparent colour; with it, solid black.
    bool dithered = false;
};
RadarLayout radarLayout(bool dashboard) noexcept;

// The radar's transparent colour key (palette index 0xff).
constexpr std::uint8_t radarTransparent = 0xFF;

// A car the radar shows: its position and the racer type field.
struct RadarCar {
    Vec3 position;
    int type = 0;
};

// draw_radar_pointer (0x40fdb4), the 8-bit radar: width * height palette
// indices. The player is the dot at the centre; the cars are placed by
// their offset from the player / 12, turned by the player's heading.
std::vector<std::uint8_t> radarPixels(const RadarLayout& layout, const Vec3& player, int heading,
                                      std::span<const RadarCar> cars);

} // namespace opena2::race
