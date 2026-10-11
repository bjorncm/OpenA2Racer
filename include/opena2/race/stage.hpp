#pragma once

// Stage data of the race (A2SPEL.DAT): the stage table, the static object and
// car type catalogues, the .OMP scenery files and the race settings in A2.INI.

#include "opena2/menu_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opena2::race {

struct StageInfo {
    const char* trackFile;
    const char* sceneryFile;
    // 1 = the track is driven forward, 0 = backward.
    bool forward;
    // Race clock in seconds unless A2.INI [TRACK] overrides it.
    std::int32_t defaultSeconds;
    // The stage music, a sample number 2..7 (start_music reads +0 of the
    // entry at 0x458d50).
    std::int32_t music;
};

// PTR_s_etappe1_trk_00458d54 and PTR_s_ETAPPE1_OMP_004590c8, stages 0..5.
const std::array<StageInfo, 6>& stageTable() noexcept;

struct StaticObjectType {
    const char* mesh;
    const char* texture;
};

// PTR_s_sky_d3d_00457c58: the 48 static object types.
const std::array<StaticObjectType, 48>& staticObjectTypes() noexcept;

namespace static_object {
constexpr int sky = 0;
constexpr int roadworks = 18;
constexpr int roadworks2 = 19;
constexpr int speedCamera = 20;
constexpr int start = 22;
constexpr int finish = 23;
constexpr int money = 24;
constexpr int fuel = 25;
constexpr int time = 26;
constexpr int number1 = 38;
} // namespace static_object

struct CarType {
    const char* mesh;
    const char* texture;
    const char* skin;
    float topSpeed;
    float mass;
};

// The car type table at 0x457978 (0x20 bytes per entry, 23 entries).
const std::array<CarType, 23>& carTypes() noexcept;

// One record of an ETAPPEn.OMP file (level_data, 0x411131).
struct SceneryRecord {
    std::int32_t piece = 0;
    std::int32_t side = 0;
    std::int32_t mode = 0;
    std::int32_t type = 0;
    std::int32_t distance = 0;
};

// Records with a negative field or a type above 47 are skipped, as the
// original does.
std::optional<std::vector<SceneryRecord>> decodeScenery(
    std::span<const std::byte> data,
    std::string& error);

// A2.INI values the race reads (FUN_0040c2f4, set_racer_place, opponent set-up).
// The defaults are the values of the A2.INI on the game CD, used when the
// file or a key is missing. The original reads a missing key as 0 (and, without
// the file, has no traffic and racers that do not move); the port keeps the
// game playable instead.
struct RaceSettings {
    // Race clock in seconds per stage: [TRACK] START1..3 * 60, mirrored for
    // the backward stages (START1=3, START2=5, START3=9).
    std::array<std::int32_t, 6> stageSeconds{180, 300, 540, 540, 300, 180};
    // [BONUS] TIME * 60, seconds added by a time bonus (TIME=2).
    std::int32_t bonusSeconds = 120;
    // [RACE] POLICESPEED and RACERSPEED, top speeds.
    std::int32_t policeSpeed = 17;
    std::int32_t racerSpeed = 16;
    // [HELPDESK] SCALE.
    std::int32_t helpdeskScale = 1;
    // [TWEAK] RACERRANGE, RACERHIGHSPEED and RACERLOWSPEED: the rubber band
    // of update_active_opponents (DAT_00473018, DAT_0047301c, DAT_00473020).
    std::int32_t racerRange = 2;
    float racerHighSpeed = 16.0F;
    float racerLowSpeed = 13.0F;
    // Police and civilian car counts (DAT_0045b018, DAT_0045b01c): [TWEAK]
    // POLICEn and CIVILn of stage n = DAT_00458c38, which is still 0 when the
    // program start reads them, so always POLICE0 and CIVIL0 (10 and 150); a
    // value of 0 becomes 8 and 150, as in the original.
    std::int32_t policeCount = 10;
    std::int32_t civilCount = 150;

    static RaceSettings fromIni(const IniValues& values);
};

} // namespace opena2::race
