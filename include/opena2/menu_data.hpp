#pragma once

// Persistent data of the original A2 Racer menu (A2MENU.DAT), kept in the
// original binary layouts: OPTIE.A2R, BEDIEN.A2R, SLOT.A2R, SCORE.A2R and
// INFIO.INI, the file the menu and the race exchange.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opena2 {

// OPTIE.A2R: ten little-endian int16 (opties, 0x437220).
struct GameOptions {
    static constexpr std::size_t fileSize = 20;
    static constexpr int maximumVolume = 4;

    std::int16_t view = 1;
    std::int16_t resolution = 1;
    // GELUID: 0 effects and music, 1 effects only, 2 no sound, 3 music only.
    std::int16_t sound = 0;
    // DISPLAY_MODE: 0 3D Accelerator, 1 320 x 200, 2 640 x 480.
    std::int16_t displayMode = 1;
    std::int16_t horizon = 0;
    std::int16_t dashboard = 0;
    std::int16_t detail = 1;
    std::int16_t camera = 0;
    std::int16_t musicVolume = 3;
    std::int16_t effectsVolume = 4;

    bool musicEnabled() const noexcept { return sound == 0 || sound == 3; }
    bool soundEnabled() const noexcept { return sound != 2; }

    std::array<std::uint8_t, fileSize> serialize() const;
    static std::optional<GameOptions> parse(std::span<const std::uint8_t> bytes);
    bool operator==(const GameOptions&) const = default;
};

// BEDIEN.A2R: nine little-endian int32 (0x437238). Keys are Win32
// virtual-key codes.
struct ControlSettings {
    static constexpr std::size_t fileSize = 36;
    static constexpr std::size_t keyCount = 6;
    // Keys in file order: LINKS, RECHTS, GAS, REM, TOETER, AKTIE.
    static constexpr std::size_t configurableKeys = 4;

    // 0 Toetsenbord, 1 Joypad 1, 2 Stuur.
    std::int32_t device = 0;
    std::int32_t unknown1 = 0;
    std::int32_t unknown2 = 1;
    std::array<std::int32_t, keyCount> keys{0x25, 0x27, 0x26, 0x28, 0x20, 0x0D};

    std::array<std::uint8_t, fileSize> serialize() const;
    static std::optional<ControlSettings> parse(std::span<const std::uint8_t> bytes);
    bool operator==(const ControlSettings&) const = default;
};

constexpr int stageCount = 6;
constexpr int computerRacerCount = 5;

// One player ("slot"), 316 bytes in SLOT.A2R.
struct PlayerSlot {
    static constexpr std::size_t recordSize = 0x13C;
    static constexpr std::size_t nameCapacity = 34;

    bool used = false;
    std::uint8_t saveOrder = 0;
    GameOptions options;
    ControlSettings controls;
    std::string name;
    std::int16_t padding = 0;
    // Completed stages (0..6); also the next stage to drive.
    std::int16_t stage = 0;
    std::array<std::int32_t, stageCount> playerTime{};
    std::int32_t money = 0;
    std::array<std::int16_t, stageCount> playerPosition{};
    std::array<std::array<std::int32_t, stageCount>, computerRacerCount> racerTime{};
    std::array<std::array<std::int16_t, stageCount>, computerRacerCount> racerPosition{};

    void resetResults();
    // Total time of the player over stages 0..stages-1.
    std::int32_t playerTotal(int stages) const;
    std::int32_t racerTotal(int racer, int stages) const;
};

class SlotDatabase {
public:
    static constexpr std::size_t slotCount = 10;
    static constexpr std::size_t fileSize = slotCount * PlayerSlot::recordSize;

    PlayerSlot& operator[](std::size_t index) { return slots_.at(index); }
    const PlayerSlot& operator[](std::size_t index) const { return slots_.at(index); }

    // Fresh database as written when SLOT.A2R is missing.
    static SlotDatabase empty(const GameOptions& options, const ControlSettings& controls);

    void create(std::size_t index, const std::string& name, const GameOptions& options,
                const ControlSettings& controls);
    void clear(std::size_t index);
    void reset(std::size_t index, const GameOptions& options, const ControlSettings& controls);
    std::size_t lastSaved() const noexcept;

    // Texts as the player lists show them.
    std::string nameText(std::size_t index) const;
    std::string stageText(std::size_t index) const;
    std::string timeText(std::size_t index) const;

    std::vector<std::uint8_t> serialize() const;
    static std::optional<SlotDatabase> parse(std::span<const std::uint8_t> bytes);

private:
    std::array<PlayerSlot, slotCount> slots_{};
};

struct ScoreEntry {
    std::string name;
    std::int32_t time = 0;
    std::int16_t stage = 0;
    std::int16_t isNew = 0;
    std::int32_t money = 0;

    bool operator==(const ScoreEntry&) const = default;
};

// SCORE.A2R: list 0 is the overall hiscore, lists 1..6 the stage lists.
class ScoreDatabase {
public:
    static constexpr std::size_t listCount = 7;
    static constexpr std::size_t entriesPerList = 10;
    static constexpr std::size_t entrySize = 0x2C;
    static constexpr std::size_t fileSize = listCount * entriesPerList * entrySize;

    using List = std::array<ScoreEntry, entriesPerList>;

    static ScoreDatabase defaults();

    const List& hiscore() const noexcept { return lists_[0]; }
    const List& stage(int stage) const { return lists_.at(static_cast<std::size_t>(stage) + 1); }

    // Insert and return the place of the new entry (0..9), or -1.
    int addHiscore(const std::string& name, std::int32_t time, int stage, std::int32_t money);
    int addStageScore(const std::string& name, std::int32_t time, int stage, std::int32_t money);
    void clearNewFlags();

    std::vector<std::uint8_t> serialize() const;
    static std::optional<ScoreDatabase> parse(std::span<const std::uint8_t> bytes);

private:
    static int insert(List& list, ScoreEntry entry);

    std::array<List, listCount> lists_{};
};

// INFIO.INI: settings and results passed between the menu and the race.
struct Infio {
    std::uint8_t raceFinished = 0;
    std::int32_t time = 0;
    std::array<std::int32_t, computerRacerCount> racerTime{};
    std::int16_t activeSlot = 0;
    std::int16_t sound = 0;
    std::int16_t resolution = 0;
    std::int16_t view = 0;
    std::int16_t control = 0;
    std::array<std::int16_t, ControlSettings::keyCount> keys{37, 39, 38, 40, 32, 13};
    std::int16_t displayMode = 1;
    std::int16_t horizon = 0;
    std::int16_t dashboard = 0;
    std::int16_t detail = 1;
    std::int16_t stage = 0;
    std::int16_t camera = 0;
    std::int16_t finishedBy = 0;
    std::int16_t position = 6;
    std::array<std::int16_t, computerRacerCount> racerPosition{1, 2, 3, 4, 5};
    std::int16_t finale = 0;
    std::int16_t musicVolume = 3;
    std::int16_t effectsVolume = 4;
    std::int32_t money = 1000;

    std::string serialize() const;
    static Infio parse(const std::string& text);
};

// Formats seconds the way the menu does: "00u43m20s".
std::string formatRaceTime(std::int32_t seconds);
// "Etappe 1".."Etappe 6", "Uitgereden" for 6 and 7.
std::string stageName(int stage);
// Names of the five computer racers.
const std::array<const char*, computerRacerCount>& computerRacerNames();

// Integer values of an INI file by section and key, read the way
// GetPrivateProfileInt reads them (leading integer, 0 when not a number).
using IniValues = std::map<std::string, std::map<std::string, std::int32_t>>;
IniValues parseIniValues(const std::string& text);

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& path);
bool writeFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

} // namespace opena2
