#include "opena2/menu_data.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace opena2 {
namespace {

class Writer {
public:
    explicit Writer(std::span<std::uint8_t> bytes) : bytes_(bytes) {}

    void u8(std::size_t offset, std::uint8_t value) { bytes_[offset] = value; }
    void i16(std::size_t offset, std::int16_t value)
    {
        const auto raw = static_cast<std::uint16_t>(value);
        bytes_[offset] = static_cast<std::uint8_t>(raw & 0xFF);
        bytes_[offset + 1] = static_cast<std::uint8_t>(raw >> 8);
    }
    void i32(std::size_t offset, std::int32_t value)
    {
        const auto raw = static_cast<std::uint32_t>(value);
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[offset + i] = static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFF);
        }
    }
    void text(std::size_t offset, std::size_t capacity, const std::string& value)
    {
        const auto length = std::min(value.size(), capacity - 1);
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), capacity, std::uint8_t{0});
        std::memcpy(bytes_.data() + offset, value.data(), length);
    }
    void raw(std::size_t offset, std::span<const std::uint8_t> value)
    {
        std::copy(value.begin(), value.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

private:
    std::span<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8(std::size_t offset) const { return bytes_[offset]; }
    std::int16_t i16(std::size_t offset) const
    {
        return static_cast<std::int16_t>(bytes_[offset] | (bytes_[offset + 1] << 8));
    }
    std::int32_t i32(std::size_t offset) const
    {
        std::uint32_t raw = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            raw |= static_cast<std::uint32_t>(bytes_[offset + i]) << (8 * i);
        }
        return static_cast<std::int32_t>(raw);
    }
    std::string text(std::size_t offset, std::size_t capacity) const
    {
        std::string value;
        for (std::size_t i = 0; i < capacity && bytes_[offset + i] != 0; ++i) {
            value.push_back(static_cast<char>(bytes_[offset + i]));
        }
        return value;
    }
    std::span<const std::uint8_t> raw(std::size_t offset, std::size_t size) const
    {
        return bytes_.subspan(offset, size);
    }

private:
    std::span<const std::uint8_t> bytes_;
};

// Ranking used by both score lists (0x403568): stage descending, then time
// ascending, then name.
bool scoreBefore(const ScoreEntry& left, const ScoreEntry& right)
{
    if (left.stage != right.stage) {
        return left.stage > right.stage;
    }
    if (left.time != right.time) {
        return left.time < right.time;
    }
    return std::strncmp(left.name.c_str(), right.name.c_str(), 32) < 0;
}

} // namespace

std::array<std::uint8_t, GameOptions::fileSize> GameOptions::serialize() const
{
    std::array<std::uint8_t, fileSize> bytes{};
    Writer writer(bytes);
    const std::array<std::int16_t, 10> fields{view, resolution, sound, displayMode, horizon,
                                              dashboard, detail, camera, musicVolume, effectsVolume};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        writer.i16(i * 2, fields[i]);
    }
    return bytes;
}

std::optional<GameOptions> GameOptions::parse(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < fileSize) {
        return std::nullopt;
    }
    Reader reader(bytes);
    GameOptions options;
    std::int16_t* fields[] = {&options.view, &options.resolution, &options.sound,
                              &options.displayMode, &options.horizon, &options.dashboard,
                              &options.detail, &options.camera, &options.musicVolume,
                              &options.effectsVolume};
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        *fields[i] = reader.i16(i * 2);
    }
    return options;
}

std::array<std::uint8_t, ControlSettings::fileSize> ControlSettings::serialize() const
{
    std::array<std::uint8_t, fileSize> bytes{};
    Writer writer(bytes);
    writer.i32(0, device);
    writer.i32(4, unknown1);
    writer.i32(8, unknown2);
    for (std::size_t i = 0; i < keyCount; ++i) {
        writer.i32(12 + i * 4, keys[i]);
    }
    return bytes;
}

std::optional<ControlSettings> ControlSettings::parse(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < fileSize) {
        return std::nullopt;
    }
    Reader reader(bytes);
    ControlSettings controls;
    controls.device = reader.i32(0);
    controls.unknown1 = reader.i32(4);
    controls.unknown2 = reader.i32(8);
    for (std::size_t i = 0; i < keyCount; ++i) {
        controls.keys[i] = reader.i32(12 + i * 4);
    }
    return controls;
}

void PlayerSlot::resetResults()
{
    for (int stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
        const auto s = static_cast<std::size_t>(stageIndex);
        playerTime[s] = 0;
        playerPosition[s] = 6;
        for (int racer = 0; racer < computerRacerCount; ++racer) {
            const auto r = static_cast<std::size_t>(racer);
            racerTime[r][s] = 0;
            racerPosition[r][s] = static_cast<std::int16_t>(racer + 1);
        }
    }
}

std::int32_t PlayerSlot::playerTotal(int stages) const
{
    std::int32_t total = 0;
    for (int s = 0; s < std::min(stages, stageCount); ++s) {
        total += playerTime[static_cast<std::size_t>(s)];
    }
    return total;
}

std::int32_t PlayerSlot::racerTotal(int racer, int stages) const
{
    std::int32_t total = 0;
    for (int s = 0; s < std::min(stages, stageCount); ++s) {
        total += racerTime.at(static_cast<std::size_t>(racer))[static_cast<std::size_t>(s)];
    }
    return total;
}

SlotDatabase SlotDatabase::empty(const GameOptions& options, const ControlSettings& controls)
{
    SlotDatabase database;
    for (auto& slot : database.slots_) {
        slot.options = options;
        slot.controls = controls;
    }
    return database;
}

// create_slot (0x401dd4).
void SlotDatabase::create(std::size_t index, const std::string& name, const GameOptions& options,
                          const ControlSettings& controls)
{
    std::uint8_t highest = 0;
    for (std::size_t i = 0; i < slotCount; ++i) {
        if (i != index) {
            highest = std::max(highest, slots_[i].saveOrder);
        }
    }
    auto& slot = slots_.at(index);
    slot.name = name.substr(0, PlayerSlot::nameCapacity - 1);
    slot.saveOrder = static_cast<std::uint8_t>(highest + 1);
    slot.stage = 0;
    slot.used = true;
    slot.money = 1000;
    slot.options = options;
    slot.controls = controls;
    slot.resetResults();
}

// clear_slot (0x402267). Name and stage stay in the record.
void SlotDatabase::clear(std::size_t index)
{
    auto& slot = slots_.at(index);
    for (auto& other : slots_) {
        if (slot.saveOrder < other.saveOrder) {
            --other.saveOrder;
        }
    }
    slot.used = false;
    slot.saveOrder = 0;
    slot.money = 0;
    slot.resetResults();
}

// reset_slot (0x402601), used by "Opnieuw".
void SlotDatabase::reset(std::size_t index, const GameOptions& options,
                         const ControlSettings& controls)
{
    auto& slot = slots_.at(index);
    slot.saveOrder = 0;
    slot.stage = 0;
    slot.money = 1000;
    slot.options = options;
    slot.controls = controls;
    slot.resetResults();
}

// get_last_saved_slot (0x402b9f).
std::size_t SlotDatabase::lastSaved() const noexcept
{
    std::uint8_t best = 0;
    std::size_t index = 0;
    for (std::size_t i = 0; i < slotCount; ++i) {
        if (slots_[i].used && slots_[i].saveOrder > best) {
            best = slots_[i].saveOrder;
            index = i;
        }
    }
    return index;
}

std::string SlotDatabase::nameText(std::size_t index) const
{
    const auto& slot = slots_.at(index);
    return slot.used ? slot.name : "L E E G";
}

std::string SlotDatabase::stageText(std::size_t index) const
{
    const auto& slot = slots_.at(index);
    return slot.used ? stageName(slot.stage) : "";
}

std::string SlotDatabase::timeText(std::size_t index) const
{
    const auto& slot = slots_.at(index);
    return slot.used ? formatRaceTime(slot.playerTotal(slot.stage)) : "--u--m--s";
}

std::vector<std::uint8_t> SlotDatabase::serialize() const
{
    std::vector<std::uint8_t> bytes(fileSize);
    for (std::size_t index = 0; index < slotCount; ++index) {
        const auto& slot = slots_[index];
        Writer writer(std::span(bytes).subspan(index * PlayerSlot::recordSize, PlayerSlot::recordSize));
        writer.u8(0x00, slot.used ? 1 : 0);
        writer.u8(0x01, slot.saveOrder);
        writer.raw(0x02, slot.options.serialize());
        writer.raw(0x16, slot.controls.serialize());
        writer.text(0x3A, PlayerSlot::nameCapacity, slot.name);
        writer.i16(0x5C, slot.padding);
        writer.i16(0x5E, slot.stage);
        for (std::size_t s = 0; s < stageCount; ++s) {
            writer.i32(0x60 + s * 4, slot.playerTime[s]);
            writer.i16(0x7C + s * 2, slot.playerPosition[s]);
            for (std::size_t r = 0; r < computerRacerCount; ++r) {
                writer.i32(0x88 + r * 0x18 + s * 4, slot.racerTime[r][s]);
                writer.i16(0x100 + r * 0x0C + s * 2, slot.racerPosition[r][s]);
            }
        }
        writer.i32(0x78, slot.money);
    }
    return bytes;
}

std::optional<SlotDatabase> SlotDatabase::parse(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < fileSize) {
        return std::nullopt;
    }
    SlotDatabase database;
    for (std::size_t index = 0; index < slotCount; ++index) {
        auto& slot = database.slots_[index];
        Reader reader(bytes.subspan(index * PlayerSlot::recordSize, PlayerSlot::recordSize));
        slot.used = reader.u8(0x00) != 0;
        slot.saveOrder = reader.u8(0x01);
        slot.options = *GameOptions::parse(reader.raw(0x02, GameOptions::fileSize));
        slot.controls = *ControlSettings::parse(reader.raw(0x16, ControlSettings::fileSize));
        slot.name = reader.text(0x3A, PlayerSlot::nameCapacity);
        slot.padding = reader.i16(0x5C);
        slot.stage = reader.i16(0x5E);
        for (std::size_t s = 0; s < stageCount; ++s) {
            slot.playerTime[s] = reader.i32(0x60 + s * 4);
            slot.playerPosition[s] = reader.i16(0x7C + s * 2);
            for (std::size_t r = 0; r < computerRacerCount; ++r) {
                slot.racerTime[r][s] = reader.i32(0x88 + r * 0x18 + s * 4);
                slot.racerPosition[r][s] = reader.i16(0x100 + r * 0x0C + s * 2);
            }
        }
        slot.money = reader.i32(0x78);
    }
    return database;
}

// The table SCORE.A2R is created from when it is missing (0x432194).
ScoreDatabase ScoreDatabase::defaults()
{
    static constexpr const char* hiscoreNames[] = {"Jeroen", "David", "Rogier", "Erik", "Rudolf",
                                                   "Richard", "Brian", "Gijs", "Milan", "Mirko"};
    static constexpr std::int32_t hiscoreTimes[] = {2600, 2700, 2800, 2900, 3000,
                                                    3100, 3200, 3400, 3500, 3600};
    static constexpr std::int16_t hiscoreStages[] = {6, 6, 5, 5, 4, 4, 3, 3, 2, 2};
    static constexpr const char* stageNames[stageCount][entriesPerList] = {
        {"Arnoud", "Peter", "Rogier", "Jeroen", "Jack", "Richard", "Rudolf", "Stefan", "Harry", "Coen"},
        {"Karin", "Johan", "David", "Brian", "Rudolf", "Erik", "Stefanie", "Ronald", "Hans", "Jeroen"},
        {"Ilma", "George", "Leo", "Sonja", "Patrick", "Richard", "Brian", "Evelyne", "Milan", "Rudi"},
        {"Sander", "Rudolf", "Harry", "Erik", "Rudolf", "Richard", "Brian", "Hans", "Dave", "Jim"},
        {"Ken", "Jeroen", "Mark", "Anne", "Paul", "Jan", "Frans", "Bianca", "Ingrid", "Willem"},
        {"Jeroen", "David", "Rogier", "Erik", "Rudolf", "Richard", "Brian", "Gijs", "Milan", "Mirko"},
    };
    static constexpr std::int32_t stageTimes[] = {600, 700, 800, 900, 1000,
                                                  1100, 1200, 1400, 1500, 1600};
    ScoreDatabase database;
    for (std::size_t i = 0; i < entriesPerList; ++i) {
        database.lists_[0][i] = {hiscoreNames[i], hiscoreTimes[i], hiscoreStages[i], 0, 0};
        for (std::size_t s = 0; s < stageCount; ++s) {
            database.lists_[s + 1][i] = {stageNames[s][i], stageTimes[i], 0, 0, 0};
        }
    }
    return database;
}

int ScoreDatabase::insert(List& list, ScoreEntry entry)
{
    for (auto& existing : list) {
        existing.isNew = 0;
    }
    std::vector<ScoreEntry> ranked(list.begin(), list.end());
    entry.isNew = 1;
    ranked.push_back(std::move(entry));
    std::stable_sort(ranked.begin(), ranked.end(), scoreBefore);
    int place = -1;
    for (std::size_t i = 0; i < entriesPerList; ++i) {
        list[i] = ranked[i];
        if (list[i].isNew != 0) {
            place = static_cast<int>(i);
        }
    }
    return place;
}

// add_new_hiscore (0x4035a2).
int ScoreDatabase::addHiscore(const std::string& name, std::int32_t time, int stage,
                              std::int32_t money)
{
    return insert(lists_[0], {name.substr(0, 31), time, static_cast<std::int16_t>(stage), 1, money});
}

// add_new_levelscore (0x403720).
int ScoreDatabase::addStageScore(const std::string& name, std::int32_t time, int stage,
                                 std::int32_t money)
{
    return insert(lists_.at(static_cast<std::size_t>(stage) + 1), {name.substr(0, 31), time, 0, 1, money});
}

void ScoreDatabase::clearNewFlags()
{
    for (auto& list : lists_) {
        for (auto& entry : list) {
            entry.isNew = 0;
        }
    }
}

std::vector<std::uint8_t> ScoreDatabase::serialize() const
{
    std::vector<std::uint8_t> bytes(fileSize);
    for (std::size_t l = 0; l < listCount; ++l) {
        for (std::size_t i = 0; i < entriesPerList; ++i) {
            const auto& entry = lists_[l][i];
            Writer writer(std::span(bytes).subspan((l * entriesPerList + i) * entrySize, entrySize));
            writer.text(0x00, 32, entry.name);
            writer.i32(0x20, entry.time);
            writer.i16(0x24, entry.stage);
            writer.i16(0x26, entry.isNew);
            writer.i32(0x28, entry.money);
        }
    }
    return bytes;
}

std::optional<ScoreDatabase> ScoreDatabase::parse(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < fileSize) {
        return std::nullopt;
    }
    ScoreDatabase database;
    for (std::size_t l = 0; l < listCount; ++l) {
        for (std::size_t i = 0; i < entriesPerList; ++i) {
            Reader reader(bytes.subspan((l * entriesPerList + i) * entrySize, entrySize));
            database.lists_[l][i] = {reader.text(0x00, 32), reader.i32(0x20), reader.i16(0x24),
                                     reader.i16(0x26), reader.i32(0x28)};
        }
    }
    return database;
}

namespace {

struct IniField {
    const char* section;
    const char* key;
};

} // namespace

// GetPrivateProfileInt: leading integer, 0 when missing.
IniValues parseIniValues(const std::string& text)
{
    std::map<std::string, std::map<std::string, std::int32_t>> values;
    std::istringstream lines(text);
    std::string line;
    std::string section;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 2 && line.front() == '[' && line.find(']') != std::string::npos) {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos || section.empty()) {
            continue;
        }
        std::int32_t value = 0;
        std::sscanf(line.c_str() + equals + 1, "%d", &value);
        values[section][line.substr(0, equals)] = value;
    }
    return values;
}

std::string Infio::serialize() const
{
    std::ostringstream out;
    out << "[GAME]\n"
        << "RACE_KLAAR=" << int{raceFinished} << '\n'
        << "TIJD=" << time << '\n';
    for (std::size_t i = 0; i < racerTime.size(); ++i) {
        out << "TIJD_" << i + 1 << '=' << racerTime[i] << '\n';
    }
    out << "ACTIVE_SLOT=" << activeSlot << '\n'
        << "ETAPPE=" << stage << '\n'
        << "EINDE_DOOR=" << finishedBy << '\n'
        << "POSITIE=" << position << '\n';
    for (std::size_t i = 0; i < racerPosition.size(); ++i) {
        out << "RACER_" << i + 1 << '=' << racerPosition[i] << '\n';
    }
    out << "FINALE=" << finale << '\n'
        << "GELD=" << money << '\n'
        << "[OPTION]\n"
        << "GELUID=" << sound << '\n'
        << "RESOLUTIE=" << resolution << '\n'
        << "VIEW=" << view << '\n'
        << "DISPLAY_MODE=" << displayMode << '\n'
        << "HORIZON=" << horizon << '\n'
        << "DASHBORD=" << dashboard << '\n'
        << "DETAIL=" << detail << '\n'
        << "CAMERA=" << camera << '\n'
        << "VMUZIEK=" << musicVolume << '\n'
        << "VEFFECT=" << effectsVolume << '\n'
        << "[CONTROL]\n"
        << "BEDIENING=" << control << '\n';
    static constexpr const char* keyNames[] = {"KEY_LINKS", "KEY_RECHTS", "KEY_GAS",
                                               "KEY_REM", "KEY_ACTIE1", "KEY_ACTIE2"};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        out << keyNames[i] << '=' << keys[i] << '\n';
    }
    return out.str();
}

Infio Infio::parse(const std::string& text)
{
    auto values = parseIniValues(text);
    const auto get = [&values](const char* section, const std::string& key) {
        const auto found = values.find(section);
        if (found == values.end()) {
            return std::int32_t{0};
        }
        const auto value = found->second.find(key);
        return value == found->second.end() ? std::int32_t{0} : value->second;
    };
    const auto i16 = [&get](const char* section, const std::string& key) {
        return static_cast<std::int16_t>(get(section, key));
    };
    Infio infio;
    infio.raceFinished = static_cast<std::uint8_t>(get("GAME", "RACE_KLAAR"));
    infio.time = get("GAME", "TIJD");
    for (std::size_t i = 0; i < infio.racerTime.size(); ++i) {
        // The menu reads these as int16 and sign-extends them.
        infio.racerTime[i] = i16("GAME", "TIJD_" + std::to_string(i + 1));
        infio.racerPosition[i] = i16("GAME", "RACER_" + std::to_string(i + 1));
    }
    infio.activeSlot = i16("GAME", "ACTIVE_SLOT");
    infio.stage = i16("GAME", "ETAPPE");
    infio.finishedBy = i16("GAME", "EINDE_DOOR");
    infio.position = i16("GAME", "POSITIE");
    infio.finale = i16("GAME", "FINALE");
    infio.money = get("GAME", "GELD");
    infio.sound = i16("OPTION", "GELUID");
    infio.resolution = i16("OPTION", "RESOLUTIE");
    infio.view = i16("OPTION", "VIEW");
    infio.displayMode = i16("OPTION", "DISPLAY_MODE");
    infio.horizon = i16("OPTION", "HORIZON");
    infio.dashboard = i16("OPTION", "DASHBORD");
    infio.detail = i16("OPTION", "DETAIL");
    infio.camera = i16("OPTION", "CAMERA");
    infio.musicVolume = i16("OPTION", "VMUZIEK");
    infio.effectsVolume = i16("OPTION", "VEFFECT");
    infio.control = i16("CONTROL", "BEDIENING");
    static constexpr const char* keyNames[] = {"KEY_LINKS", "KEY_RECHTS", "KEY_GAS",
                                               "KEY_REM", "KEY_ACTIE1", "KEY_ACTIE2"};
    for (std::size_t i = 0; i < infio.keys.size(); ++i) {
        infio.keys[i] = i16("CONTROL", keyNames[i]);
    }
    return infio;
}

std::string formatRaceTime(std::int32_t seconds)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%02du%02dm%02ds", seconds / 3600, (seconds / 60) % 60,
                  seconds % 60);
    return text;
}

std::string stageName(int stage)
{
    if (stage >= 0 && stage < stageCount) {
        return "Etappe " + std::to_string(stage + 1);
    }
    return "Uitgereden";
}

const std::array<const char*, computerRacerCount>& computerRacerNames()
{
    static constexpr std::array<const char*, computerRacerCount> names{
        "Jeroen Lutos", "Gijs Comara", "Rudolf Porge", "Richard Viber", "Brian Jakuar"};
    return names;
}

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

bool writeFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace opena2
