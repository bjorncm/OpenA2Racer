#include "opena2/menu_data.hpp"
#include "opena2/screen_transform.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

std::int32_t readI32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::int32_t>(bytes[offset] | (bytes[offset + 1] << 8) |
                                     (bytes[offset + 2] << 16) |
                                     (static_cast<std::uint32_t>(bytes[offset + 3]) << 24));
}

std::int16_t readI16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::int16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

bool testGameOptions()
{
    bool ok = true;
    const opena2::GameOptions defaults;
    const auto bytes = defaults.serialize();
    ok &= require(bytes.size() == 20, "OPTIE.A2R is 20 bytes.");
    const std::vector<std::uint8_t> expected{1, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                             0, 0, 1, 0, 0, 0, 3, 0, 4, 0};
    ok &= require(std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == expected,
                  "Default options serialize as ten little-endian int16 in file order.");

    opena2::GameOptions options;
    options.view = 2;
    options.resolution = 0;
    options.sound = 3;
    options.displayMode = 2;
    options.horizon = 1;
    options.dashboard = 1;
    options.detail = 0;
    options.camera = 2;
    options.musicVolume = 0;
    options.effectsVolume = -1;
    const auto parsed = opena2::GameOptions::parse(options.serialize());
    ok &= require(parsed && *parsed == options, "Options survive a round trip.");
    ok &= require(!opena2::GameOptions::parse(std::vector<std::uint8_t>(19)),
                  "A short OPTIE.A2R is rejected.");

    opena2::GameOptions sound;
    sound.sound = 0;
    ok &= require(sound.musicEnabled() && sound.soundEnabled(), "GELUID 0 plays music and effects.");
    sound.sound = 1;
    ok &= require(!sound.musicEnabled() && sound.soundEnabled(), "GELUID 1 plays effects only.");
    sound.sound = 2;
    ok &= require(!sound.musicEnabled() && !sound.soundEnabled(), "GELUID 2 is silent.");
    sound.sound = 3;
    ok &= require(sound.musicEnabled() && sound.soundEnabled(), "GELUID 3 plays music.");
    return ok;
}

bool testControlSettings()
{
    bool ok = true;
    const opena2::ControlSettings defaults;
    const auto raw = defaults.serialize();
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    ok &= require(bytes.size() == 36, "BEDIEN.A2R is 36 bytes.");
    ok &= require(readI32(bytes, 0) == 0 && readI32(bytes, 4) == 0 && readI32(bytes, 8) == 1,
                  "Default device and unknown fields.");
    ok &= require(readI32(bytes, 12) == 0x25 && readI32(bytes, 16) == 0x27 &&
                  readI32(bytes, 20) == 0x26 && readI32(bytes, 24) == 0x28 &&
                  readI32(bytes, 28) == 0x20 && readI32(bytes, 32) == 0x0D,
                  "Default keys are arrows, Space and Enter in file order.");

    opena2::ControlSettings controls;
    controls.device = 2;
    controls.unknown1 = 7;
    controls.unknown2 = -3;
    controls.keys = {'A', 'D', 'W', 'S', 0x10, 0x12};
    const auto parsed = opena2::ControlSettings::parse(controls.serialize());
    ok &= require(parsed && *parsed == controls, "Controls survive a round trip.");
    ok &= require(!opena2::ControlSettings::parse(std::vector<std::uint8_t>(35)),
                  "A short BEDIEN.A2R is rejected.");
    return ok;
}

bool testSlotDatabase()
{
    bool ok = true;
    opena2::GameOptions options;
    options.view = 2;
    opena2::ControlSettings controls;
    controls.device = 1;

    auto slots = opena2::SlotDatabase::empty(options, controls);
    ok &= require(opena2::SlotDatabase::fileSize == 3160 && slots.serialize().size() == 3160,
                  "SLOT.A2R holds ten 316-byte records.");
    bool emptyOk = true;
    for (std::size_t i = 0; i < opena2::SlotDatabase::slotCount; ++i) {
        emptyOk = emptyOk && !slots[i].used && slots[i].options == options &&
                  slots[i].controls == controls && slots.nameText(i) == "L E E G" &&
                  slots.stageText(i).empty() && slots.timeText(i) == "--u--m--s";
    }
    ok &= require(emptyOk, "An empty database has unused slots with the given settings.");
    ok &= require(slots.lastSaved() == 0, "Without used slots the last saved slot is 0.");

    opena2::GameOptions playerOptions;
    playerOptions.detail = 0;
    slots.create(3, "Anna", playerOptions, controls);
    const auto& anna = slots[3];
    ok &= require(anna.used && anna.saveOrder == 1 && anna.stage == 0 && anna.money == 1000 &&
                  anna.name == "Anna" && anna.options == playerOptions,
                  "create_slot fills a new player.");
    bool resultsOk = true;
    for (std::size_t s = 0; s < opena2::stageCount; ++s) {
        resultsOk = resultsOk && anna.playerTime[s] == 0 && anna.playerPosition[s] == 6;
        for (std::size_t r = 0; r < opena2::computerRacerCount; ++r) {
            resultsOk = resultsOk && anna.racerTime[r][s] == 0 &&
                        anna.racerPosition[r][s] == static_cast<std::int16_t>(r + 1);
        }
    }
    ok &= require(resultsOk, "New players start sixth with racers in order 1..5.");
    ok &= require(slots.nameText(3) == "Anna" && slots.stageText(3) == "Etappe 1" &&
                  slots.timeText(3) == "00u00m00s",
                  "List texts of a new player.");

    slots.create(5, "Bert", options, controls);
    ok &= require(slots[5].saveOrder == 2 && slots.lastSaved() == 5,
                  "Each new player gets the next save order and becomes the last saved.");
    slots.create(7, std::string(40, 'x'), options, controls);
    ok &= require(slots[7].name.size() == 33 && slots[7].saveOrder == 3,
                  "Names are cut to 33 characters.");

    slots[5].playerTime[0] = 100;
    slots[5].playerTime[1] = 200;
    slots[5].playerTime[2] = 999;
    slots[5].stage = 2;
    ok &= require(slots.timeText(5) == "00u05m00s" && slots.stageText(5) == "Etappe 3",
                  "The time text sums the completed stages only.");
    ok &= require(slots[5].playerTotal(6) == 1299 && slots[5].playerTotal(1) == 100,
                  "playerTotal sums the first stages.");
    slots[5].racerTime[2][0] = 50;
    slots[5].racerTime[2][1] = 70;
    ok &= require(slots[5].racerTotal(2, 2) == 120, "racerTotal sums a racer's stages.");

    slots.clear(3);
    ok &= require(!slots[3].used && slots[3].saveOrder == 0 && slots[3].money == 0 &&
                  slots[3].name == "Anna",
                  "clear_slot frees the slot but keeps the name.");
    ok &= require(slots[5].saveOrder == 1 && slots[7].saveOrder == 2,
                  "Clearing a slot moves later save orders down.");
    ok &= require(slots.lastSaved() == 7, "The highest save order is the last saved slot.");

    slots[7].stage = 4;
    slots[7].money = 4321;
    slots[7].playerTime[0] = 77;
    slots.reset(7, playerOptions, controls);
    ok &= require(slots[7].used && slots[7].saveOrder == 0 && slots[7].stage == 0 &&
                  slots[7].money == 1000 && slots[7].playerTime[0] == 0 &&
                  slots[7].options == playerOptions,
                  "reset_slot restarts the player and clears its save order.");
    ok &= require(slots.lastSaved() == 5, "A reset slot is no longer the last saved.");

    auto finished = slots;
    finished[5].stage = 6;
    finished[5].money = 9876;
    finished[5].playerTime[5] = 88;
    finished.restartCareer(5);
    ok &= require(finished[5].used && finished[5].saveOrder == 1 && finished[5].stage == 0 &&
                  finished[5].money == 9876 && finished[5].playerTime[5] == 0 &&
                  finished[5].name == "Bert",
                  "Restarting a finished career keeps the money and save order.");

    slots[5].padding = 0x1234;
    slots[5].money = 123456;
    slots[5].playerPosition[1] = 3;
    slots[5].racerPosition[4][5] = 2;
    slots[5].racerTime[4][5] = -1;
    const auto bytes = slots.serialize();
    const std::size_t base = 5 * 0x13C;
    ok &= require(bytes[base] == 1 && bytes[base + 1] == 1 && readI16(bytes, base + 2) == 2 &&
                  readI32(bytes, base + 0x16) == 1 && bytes[base + 0x3A] == 'B' &&
                  readI16(bytes, base + 0x5C) == 0x1234 && readI16(bytes, base + 0x5E) == 2 &&
                  readI32(bytes, base + 0x60) == 100 && readI32(bytes, base + 0x78) == 123456 &&
                  readI16(bytes, base + 0x7C + 2) == 3 &&
                  readI32(bytes, base + 0x88 + 2 * 0x18) == 50 &&
                  readI32(bytes, base + 0x88 + 4 * 0x18 + 5 * 4) == -1 &&
                  readI16(bytes, base + 0x100 + 4 * 0x0C + 5 * 2) == 2,
                  "Slot fields are at the original offsets.");
    const auto parsed = opena2::SlotDatabase::parse(bytes);
    ok &= require(parsed && parsed->serialize() == bytes, "SLOT.A2R survives a round trip.");
    ok &= require(parsed && (*parsed)[5].name == "Bert" && (*parsed)[5].money == 123456 &&
                  (*parsed)[3].name == "Anna" && !(*parsed)[3].used,
                  "Parsed slots keep their fields.");
    ok &= require(!opena2::SlotDatabase::parse(std::vector<std::uint8_t>(3159)),
                  "A short SLOT.A2R is rejected.");
    return ok;
}

bool testScoreDatabase()
{
    bool ok = true;
    auto scores = opena2::ScoreDatabase::defaults();
    const auto& hiscore = scores.hiscore();
    ok &= require(hiscore[0] == opena2::ScoreEntry{"Jeroen", 2600, 6, 0, 0} &&
                  hiscore[9] == opena2::ScoreEntry{"Mirko", 3600, 2, 0, 0},
                  "Default hiscore list.");
    ok &= require(scores.stage(0)[0] == opena2::ScoreEntry{"Arnoud", 600, 0, 0, 0} &&
                  scores.stage(0)[9].time == 1600 && scores.stage(5)[9].name == "Mirko" &&
                  scores.stage(3)[4].name == "Rudolf",
                  "Default stage lists.");

    ok &= require(scores.addHiscore("Bjorn", 2650, 6, 1500) == 1,
                  "A stage-6 finish slots in by time.");
    ok &= require(hiscore[1] == opena2::ScoreEntry{"Bjorn", 2650, 6, 1, 1500} &&
                  hiscore[2].name == "David" && hiscore[9].name == "Milan",
                  "The new entry moves the rest down and the last one drops out.");
    ok &= require(scores.addHiscore("Slow", 9999, 6, 0) == 3,
                  "More completed stages rank above a better time.");
    ok &= require(hiscore[1].isNew == 0 && hiscore[3].isNew == 1,
                  "Only the latest entry is marked new.");
    ok &= require(scores.addHiscore("Late", 10, 1, 0) == -1 && hiscore[9].name == "Gijs",
                  "An entry below the list is not added.");

    ok &= require(scores.addStageScore("Zed", 600, 0, 5) == 1,
                  "Equal times are ordered by name.");
    ok &= require(scores.addStageScore("Aaa", 600, 0, 5) == 0, "A smaller name ranks first.");
    ok &= require(scores.stage(0)[1].name == "Arnoud" && scores.stage(0)[2].name == "Zed" &&
                  scores.stage(0)[2].isNew == 0 && scores.stage(0)[0].isNew == 1,
                  "Stage lists keep their order and new flag.");
    ok &= require(scores.stage(0)[0].stage == 0 && scores.stage(0)[0].money == 5,
                  "Stage entries store stage 0 and the money.");
    ok &= require(scores.addStageScore("Zed", 600, 0, 5) == 3,
                  "An exact tie goes after the existing entries.");
    ok &= require(scores.addStageScore("Nobody", 1601, 2, 0) == -1 &&
                  scores.stage(2)[9].name == "Rudi",
                  "Slow stage times are not added.");
    ok &= require(scores.addStageScore("Fast", 1, 5, 0) == 0 && scores.stage(5)[0].name == "Fast" &&
                  scores.stage(4)[0].name == "Ken",
                  "Each stage has its own list.");
    ok &= require(scores.addStageScore(std::string(40, 'n'), 1, 1, 0) == 0 &&
                  scores.stage(1)[0].name.size() == 31,
                  "Score names are cut to 31 characters.");

    scores.clearNewFlags();
    bool cleared = true;
    for (int list = -1; list < opena2::stageCount; ++list) {
        for (const auto& entry : list < 0 ? scores.hiscore() : scores.stage(list)) {
            cleared = cleared && entry.isNew == 0;
        }
    }
    ok &= require(cleared, "clearNewFlags clears every list.");

    scores.addHiscore("Neu", 2000, 6, 777);
    const auto bytes = scores.serialize();
    ok &= require(bytes.size() == 3080, "SCORE.A2R holds seven lists of ten 44-byte entries.");
    ok &= require(bytes[0] == 'N' && bytes[3] == 0 && readI32(bytes, 0x20) == 2000 &&
                  readI16(bytes, 0x24) == 6 && readI16(bytes, 0x26) == 1 &&
                  readI32(bytes, 0x28) == 777,
                  "Score fields are at the original offsets.");
    ok &= require(bytes[10 * 0x2C] == 'A' && readI32(bytes, 10 * 0x2C + 0x20) == 600,
                  "Stage 1's list follows the hiscore list.");
    const auto parsed = opena2::ScoreDatabase::parse(bytes);
    ok &= require(parsed && parsed->serialize() == bytes && parsed->hiscore() == scores.hiscore(),
                  "SCORE.A2R survives a round trip.");
    ok &= require(!opena2::ScoreDatabase::parse(std::vector<std::uint8_t>(3079)),
                  "A short SCORE.A2R is rejected.");
    return ok;
}

bool testInfio()
{
    bool ok = true;
    const opena2::Infio defaults;
    const auto text = defaults.serialize();
    ok &= require(text.rfind("[GAME]\n", 0) == 0 && text.find("POSITIE=6\n") != std::string::npos &&
                  text.find("GELD=1000\n") != std::string::npos &&
                  text.find("[OPTION]\n") != std::string::npos &&
                  text.find("KEY_ACTIE2=13\n") != std::string::npos,
                  "INFIO.INI uses the original sections and keys.");

    opena2::Infio infio;
    infio.raceFinished = 1;
    infio.time = 654;
    infio.racerTime = {601, 602, 603, 604, 605};
    infio.activeSlot = 4;
    infio.sound = 3;
    infio.resolution = 1;
    infio.view = 2;
    infio.control = 1;
    infio.keys = {65, 68, 87, 83, 16, 18};
    infio.displayMode = 2;
    infio.horizon = 1;
    infio.dashboard = 1;
    infio.detail = 0;
    infio.stage = 5;
    infio.camera = 1;
    infio.finishedBy = 2;
    infio.position = 3;
    infio.racerPosition = {2, 1, 6, 4, 5};
    infio.finale = 1;
    infio.musicVolume = 0;
    infio.effectsVolume = 2;
    infio.money = 2500;
    const auto parsed = opena2::Infio::parse(infio.serialize());
    ok &= require(parsed.serialize() == infio.serialize() && parsed.raceFinished == 1 &&
                  parsed.time == 654 && parsed.racerTime[4] == 605 && parsed.activeSlot == 4 &&
                  parsed.keys[5] == 18 && parsed.stage == 5 && parsed.position == 3 &&
                  parsed.racerPosition[2] == 6 && parsed.money == 2500 && parsed.view == 2,
                  "INFIO.INI survives a round trip.");

    const auto missing = opena2::Infio::parse("");
    ok &= require(missing.position == 0 && missing.money == 0 && missing.racerPosition[0] == 0 &&
                  missing.keys[0] == 0,
                  "Missing keys read as 0, like GetPrivateProfileInt.");
    const auto windows = opena2::Infio::parse(
        "[GAME]\r\nRACE_KLAAR=1\r\nETAPPE=5\r\nTIJD_1=40000\r\nGELD=-5\r\n[CONTROL]\r\nKEY_GAS=38\r\n");
    ok &= require(windows.raceFinished == 1 && windows.stage == 5 && windows.money == -5 &&
                  windows.keys[2] == 38,
                  "CRLF files are read.");
    ok &= require(windows.racerTime[0] == -25536, "Racer times are read as int16.");
    const auto sections = opena2::Infio::parse("[OPTION]\nETAPPE=3\n[GAME]\nPOSITIE=2\n");
    ok &= require(sections.stage == 0 && sections.position == 2, "Keys belong to their section.");
    return ok;
}

bool testTexts()
{
    bool ok = true;
    ok &= require(opena2::formatRaceTime(0) == "00u00m00s" &&
                  opena2::formatRaceTime(2600) == "00u43m20s" &&
                  opena2::formatRaceTime(3661) == "01u01m01s" &&
                  opena2::formatRaceTime(36000 + 59 * 60 + 59) == "10u59m59s",
                  "Race times are formatted as hours, minutes and seconds.");
    ok &= require(opena2::stageName(0) == "Etappe 1" && opena2::stageName(5) == "Etappe 6" &&
                  opena2::stageName(6) == "Uitgereden" && opena2::stageName(7) == "Uitgereden",
                  "Stage names.");
    ok &= require(std::string(opena2::computerRacerNames()[0]) == "Jeroen Lutos" &&
                  std::string(opena2::computerRacerNames()[4]) == "Brian Jakuar",
                  "Computer racer names.");
    return ok;
}

bool testFiles()
{
    bool ok = true;
    const auto path = std::filesystem::temp_directory_path() / "opena2_menu_test.bin";
    const std::vector<std::uint8_t> bytes{0, 1, 2, 0xFF, 0x0D, 0x0A};
    ok &= require(opena2::writeFileBytes(path, bytes), "Files can be written.");
    const auto read = opena2::readFileBytes(path);
    ok &= require(read && *read == bytes, "Files are read back byte for byte.");
    std::filesystem::remove(path);
    ok &= require(!opena2::readFileBytes(path), "A missing file reads as nothing.");
    return ok;
}

bool testScreenTransform()
{
    bool ok = true;
    const auto exact = opena2::ScreenTransform::fit(1920, 1440);
    ok &= require(exact.scale == 3.0F && exact.offsetX == 0.0F && exact.offsetY == 0.0F,
                  "A 4:3 target fills the screen.");
    const auto rect = exact.toTarget(9.0F, 27.0F, 103.0F, 45.0F);
    ok &= require(rect.x0 == 27 && rect.y0 == 81 && rect.x1 == 336 && rect.y1 == 216,
                  "Logical rectangles map to target pixels.");

    const auto wide = opena2::ScreenTransform::fit(2560, 1440);
    ok &= require(wide.scale == 3.0F && wide.offsetX == 320.0F && wide.offsetY == 0.0F,
                  "Wide targets are pillarboxed.");
    const auto [x, y] = wide.toLogical(320.0F + 30.0F, 90.0F);
    ok &= require(x == 10.0F && y == 30.0F, "Target points map back to logical space.");

    const auto fractional = opena2::ScreenTransform::fit(960, 720);
    const auto left = fractional.toTarget(0.0F, 0.0F, 103.0F, 45.0F);
    const auto right = fractional.toTarget(103.0F, 0.0F, 103.0F, 45.0F);
    ok &= require(left.x1 == right.x0, "Adjacent rectangles share an edge at fractional scales.");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    for (const auto test : {testGameOptions, testControlSettings, testSlotDatabase,
                            testScoreDatabase, testInfio, testTexts, testFiles,
                            testScreenTransform}) {
        ok = test() && ok;
    }
    return ok ? 0 : 1;
}
