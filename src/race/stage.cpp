#include "opena2/race/stage.hpp"

#include <bit>
#include <cmath>

namespace opena2::race {

const std::array<StageInfo, 6>& stageTable() noexcept
{
    static const std::array<StageInfo, 6> table{{
        {"etappe1.trk", "ETAPPE1.OMP", true, 600, 2},
        {"etappe2.trk", "ETAPPE2.OMP", true, 900, 3},
        {"etappe3.trk", "ETAPPE3.OMP", true, 1200, 4},
        {"etappe3.trk", "ETAPPE3.OMP", false, 1200, 5},
        {"etappe2.trk", "ETAPPE2.OMP", false, 900, 6},
        {"etappe1.trk", "ETAPPE1.OMP", false, 600, 7},
    }};
    return table;
}

const std::array<StaticObjectType, 48>& staticObjectTypes() noexcept
{
    static const std::array<StaticObjectType, 48> table{{
        {"sky.d3d", "sky.pcx"},           {"bovw.d3d", "50.pcx"},
        {"arena.d3d", "arena.pcx"},       {"viaduct.d3d", "viaduct.pcx"},
        {"molent.d3d", "molen.pcx"},      {"shell.d3d", "shell.pcx"},
        {"roadsgn.d3d", "roadsgn.pcx"},   {"uit.d3d", "uit.pcx"},
        {"zaltboml.d3d", "zaltboml.pcx"}, {"lekbrug.d3d", "lekbrug.pcx"},
        {"gebouwg.d3d", "gebouw.pcx"},    {"gebouw2g.d3d", "tile.pcx"},
        {"gebouw3g.d3d", "tile2.pcx"},    {"trees.d3d", "trees.pcx"},
        {"wegresg2.d3d", "wegresg2.pcx"}, {"elek4.d3d", "elek4.pcx"},
        {"huisje.d3d", "huisje.pcx"},     {"rijtjes.d3d", "rijtjes.pcx"},
        {"werkweg.d3d", "werkweg.pcx"},   {"werkweg2.d3d", "werkweg2.pcx"},
        {"flits.d3d", "flits.pcx"},       {"praatp.d3d", "praatp.pcx"},
        {"start.d3d", "start.pcx"},       {"finish.d3d", "finish.pcx"},
        {"geld.d3d", "geld.pcx"},         {"benzine.d3d", "benzine.pcx"},
        {"tijd.d3d", "tijd.pcx"},         {"lantaarn.d3d", "lantaarn.pcx"},
        {"eindh.d3d", "eindh.pcx"},       {"amsterd.d3d", "amsterd.pcx"},
        {"denbosch.d3d", "denbosch.pcx"}, {"maastr.d3d", "maastr.pcx"},
        {"utrecht.d3d", "utrecht.pcx"},   {"s_maastr.d3d", "s_maastr.pcx"},
        {"s_utr.d3d", "s_utr.pcx"},       {"s_denb.d3d", "s_denb.pcx"},
        {"s_adam.d3d", "s_adam.pcx"},     {"s_eindho.d3d", "s_eindho.pcx"},
        {"number1.d3d", "number1.pcx"},   {"number2.d3d", "number2.pcx"},
        {"number3.d3d", "number3.pcx"},   {"number4.d3d", "number4.pcx"},
        {"number5.d3d", "number5.pcx"},   {"number6.d3d", "number6.pcx"},
        {"hek.d3d", "hek.pcx"},           {"koe.d3d", "koe.pcx"},
        {"100.d3d", "100.pcx"},           {"babe.d3d", "babe.pcx"},
    }};
    return table;
}

const std::array<CarType, 23>& carTypes() noexcept
{
    static const std::array<CarType, 23> table{{
        {"porche.d3d", "porche.pcx", "k_porche.pcx", 9.0F, 900.0F},
        {"interl.d3d", "il.pcx", "k_il.pcx", 7.0F, 5000.0F},
        {"camaro.d3d", "camaro.pcx", "k_camaro.pcx", 9.0F, 1500.0F},
        {"golf.d3d", "golf.pcx", "k_golf.pcx", 8.5F, 1000.0F},
        {"bmw.d3d", "bmw.pcx", "k_bmw.pcx", 10.0F, 1300.0F},
        {"van.d3d", "vanmap.pcx", "k_vanmap.pcx", 9.0F, 1000.0F},
        {"lotus.d3d", "lotus.pcx", "k_lotus.pcx", 11.0F, 1500.0F},
        {"viper.d3d", "viper.pcx", "k_viper.pcx", 10.0F, 1500.0F},
        {"panda.d3d", "panda.pcx", "k_panda.pcx", 6.0F, 800.0F},
        {"merceds.d3d", "mercedes.pcx", "k_merced.pcx", 8.0F, 900.0F},
        {"merceds.d3d", "merceds2.pcx", "k_merce2.pcx", 8.5F, 900.0F},
        {"vracht.d3d", "vracht.pcx", "k_vracht.pcx", 8.0F, 10000.0F},
        {"police.d3d", "police.pcx", "k_police.pcx", 18.0F, 900.0F},
        {"cx.d3d", "cx.pcx", "k_cx.pcx", 10.0F, 900.0F},
        {"jaguar.d3d", "jaguar.pcx", "k_jaguar.pcx", 10.0F, 900.0F},
        {"volvo.d3d", "volvo.pcx", "k_volvo.pcx", 10.0F, 900.0F},
        {"werkweg2.d3d", "werkweg2.pcx", "werkweg2.pcx", 0.0F, 10000.0F},
        {"porche.d3d", "r_porche.pcx", "r_porche.pcx", 9.0F, 900.0F},
        {"camaro.d3d", "r_camaro.pcx", "r_camaro.pcx", 9.0F, 1500.0F},
        {"bmw.d3d", "r_bmw.pcx", "r_bmw.pcx", 10.0F, 1300.0F},
        {"jaguar.d3d", "r_jaguar.pcx", "r_jaguar.pcx", 10.0F, 900.0F},
        {"lotus.d3d", "r_lotus.pcx", "r_lotus.pcx", 11.0F, 1500.0F},
        {"viper.d3d", "r_viper.pcx", "r_viper.pcx", 10.0F, 1500.0F},
    }};
    return table;
}

std::optional<std::vector<SceneryRecord>> decodeScenery(
    std::span<const std::byte> data,
    std::string& error)
{
    error.clear();
    auto u32 = [&data](std::size_t offset) {
        std::uint32_t value = 0;
        for (int i = 3; i >= 0; --i) {
            value = (value << 8) | std::to_integer<std::uint8_t>(data[offset + static_cast<std::size_t>(i)]);
        }
        return value;
    };
    if (data.size() < 4) {
        error = "Scenery file has no record count.";
        return std::nullopt;
    }
    const std::uint32_t count = u32(0);
    if (data.size() < 4 + std::uint64_t{count} * 20) {
        error = "Scenery file is shorter than its record count.";
        return std::nullopt;
    }
    std::vector<SceneryRecord> records;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 4 + std::size_t{i} * 20;
        SceneryRecord record;
        record.piece = static_cast<std::int32_t>(u32(offset));
        record.side = static_cast<std::int32_t>(u32(offset + 4));
        record.mode = static_cast<std::int32_t>(u32(offset + 8));
        record.type = static_cast<std::int32_t>(u32(offset + 12));
        record.distance = static_cast<std::int32_t>(
            std::nearbyint(std::bit_cast<float>(u32(offset + 16))));
        if (record.piece < 0 || record.side < 0 || record.mode < 0 || record.type < 0 ||
            record.type >= static_cast<std::int32_t>(staticObjectTypes().size())) {
            continue;
        }
        records.push_back(record);
    }
    return records;
}

RaceSettings RaceSettings::fromIni(const IniValues& values)
{
    // A missing key keeps the CD's value.
    auto get = [&values](const char* section, const char* key, std::int32_t fallback) {
        const auto s = values.find(section);
        if (s == values.end()) {
            return fallback;
        }
        const auto k = s->second.find(key);
        return k == s->second.end() ? fallback : k->second;
    };
    const RaceSettings defaults;
    RaceSettings settings;
    settings.helpdeskScale = get("HELPDESK", "SCALE", defaults.helpdeskScale);
    settings.bonusSeconds = get("BONUS", "TIME", defaults.bonusSeconds / 60) * 60;
    const std::int32_t start1 = get("TRACK", "START1", defaults.stageSeconds[0] / 60) * 60;
    const std::int32_t start2 = get("TRACK", "START2", defaults.stageSeconds[1] / 60) * 60;
    const std::int32_t start3 = get("TRACK", "START3", defaults.stageSeconds[2] / 60) * 60;
    settings.stageSeconds = {start1, start2, start3, start3, start2, start1};
    settings.policeSpeed = get("RACE", "POLICESPEED", defaults.policeSpeed);
    settings.racerSpeed = get("RACE", "RACERSPEED", defaults.racerSpeed);
    settings.racerRange = get("TWEAK", "RACERRANGE", defaults.racerRange);
    settings.racerHighSpeed = static_cast<float>(
        get("TWEAK", "RACERHIGHSPEED", static_cast<std::int32_t>(defaults.racerHighSpeed)));
    settings.racerLowSpeed = static_cast<float>(
        get("TWEAK", "RACERLOWSPEED", static_cast<std::int32_t>(defaults.racerLowSpeed)));
    settings.policeCount = get("TWEAK", "POLICE0", defaults.policeCount);
    settings.civilCount = get("TWEAK", "CIVIL0", defaults.civilCount);
    if (settings.policeCount == 0) {
        settings.policeCount = 8;
    }
    if (settings.civilCount == 0) {
        settings.civilCount = 150;
    }
    return settings;
}

} // namespace opena2::race
