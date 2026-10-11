#include "opena2/game_data_directory.hpp"
#include "opena2/d3d_mesh.hpp"
#include "opena2/indexed_archive.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/pcm_wave.hpp"
#include "opena2/vehicle.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                std::filesystem::path(
                    "opena2-test-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bool writeFile(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    return file.good();
}

bool writeBytes(const std::filesystem::path& path, std::initializer_list<unsigned char> bytes)
{
    std::ofstream file(path, std::ios::binary);
    for (const auto byte : bytes) {
        file.put(static_cast<char>(byte));
    }
    return file.good();
}

bool writeNamedIndex(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::uint32_t>>& entries)
{
    std::ofstream file(path, std::ios::binary);
    const auto putU16 = [&file](std::uint16_t value) {
        file.put(static_cast<char>(value & 0xff));
        file.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto putU32 = [&file](std::uint32_t value) {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            file.put(static_cast<char>((value >> shift) & 0xff));
        }
    };

    putU16(static_cast<std::uint16_t>(entries.size()));
    for (const auto& [name, offset] : entries) {
        if (name.size() > 12) {
            return false;
        }
        file.write(name.data(), static_cast<std::streamsize>(name.size()));
        for (std::size_t i = name.size(); i < 12; ++i) {
            file.put('\0');
        }
        for (int i = 0; i < 8; ++i) {
            file.put('\0');
        }
        putU32(offset);
    }
    return file.good();
}

std::vector<std::byte> makePcx()
{
    std::vector<std::byte> data(128, std::byte{0});
    data[0] = std::byte{0x0a};
    data[1] = std::byte{5};
    data[2] = std::byte{1};
    data[3] = std::byte{8};
    data[8] = std::byte{1};
    data[65] = std::byte{1};
    data[66] = std::byte{2};
    data.push_back(std::byte{10});
    data.push_back(std::byte{20});
    data.push_back(std::byte{0x0c});
    for (std::size_t i = 0; i < 256; ++i) {
        data.push_back(static_cast<std::byte>(i));
        data.push_back(static_cast<std::byte>(255 - i));
        data.push_back(std::byte{42});
    }
    return data;
}

void appendFourCc(std::vector<std::byte>& data, const char* value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        data.push_back(static_cast<std::byte>(value[i]));
    }
}

void appendU16(std::vector<std::byte>& data, std::uint16_t value)
{
    data.push_back(static_cast<std::byte>(value & 0xff));
    data.push_back(static_cast<std::byte>((value >> 8) & 0xff));
}

void appendU32(std::vector<std::byte>& data, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        data.push_back(static_cast<std::byte>((value >> shift) & 0xff));
    }
}

std::vector<std::byte> makeWave(
    std::uint16_t channels,
    std::uint32_t sampleRate,
    std::uint16_t bits,
    std::initializer_list<std::uint8_t> samples)
{
    std::vector<std::byte> data;
    appendFourCc(data, "RIFF");
    appendU32(data, 0);
    appendFourCc(data, "WAVE");
    appendFourCc(data, "fmt ");
    appendU32(data, 16);
    appendU16(data, 1);
    appendU16(data, channels);
    appendU32(data, sampleRate);
    const auto blockAlign = static_cast<std::uint16_t>(channels * (bits / 8));
    appendU32(data, sampleRate * blockAlign);
    appendU16(data, blockAlign);
    appendU16(data, bits);
    appendFourCc(data, "data");
    appendU32(data, static_cast<std::uint32_t>(samples.size()));
    for (const auto sample : samples) {
        data.push_back(static_cast<std::byte>(sample));
    }
    const auto riffSize = static_cast<std::uint32_t>(data.size() - 8);
    for (unsigned int i = 0; i < 4; ++i) {
        data[4 + i] = static_cast<std::byte>((riffSize >> (i * 8)) & 0xff);
    }
    return data;
}

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

std::vector<std::byte> makeD3DMesh()
{
    std::vector<std::byte> data;
    appendU32(data, 3);
    appendU32(data, 1);
    const std::array<std::array<float, 8>, 3> vertices{{
        {{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F}},
        {{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F}},
    }};
    for (const auto& vertex : vertices) {
        for (const auto component : vertex) {
            appendU32(data, std::bit_cast<std::uint32_t>(component));
        }
        data.push_back(std::byte{1});
    }
    appendU16(data, 0);
    appendU16(data, 1);
    appendU16(data, 2);
    data.push_back(std::byte{0});
    data.push_back(std::byte{7});
    data.push_back(std::byte{1});
    return data;
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "disc";
    const auto outside = temporary.path() / "outside.dat";
    std::filesystem::create_directories(root);
    if (!writeFile(root / "A2.EXE") || !writeFile(root / "A2MENU.DAT") || !writeFile(outside)) {
        std::cerr << "Could not create test files.\n";
        return 1;
    }
    if (!writeBytes(root / "TEST.IMG", {2, 0, 'a', 'b', 'c', 'd', 'e'}) ||
        !writeBytes(root / "TEST.IND", {2, 0, 2, 0, 0, 0, 5, 0, 0, 0}) ||
        !writeBytes(root / "BAD.IMG", {2, 0, 'x', 'y'}) ||
        !writeBytes(root / "BAD.IND", {2, 0, 2, 0, 0, 0, 1, 0, 0, 0}) ||
        !writeBytes(root / "NAMES.IMG", {'a', 'b', 'c', 'D', 'E', 'F', 'G'}) ||
        !writeNamedIndex(
            root / "NAMES.IND",
            {{"first.bin", 0}, {"second.bin", 3}, {"abcdefghijkl", 5}})) {
        std::cerr << "Could not create archive test files.\n";
        return 1;
    }

    std::string error;
    auto gameData = opena2::GameDataDirectory::open(root, error);
    if (!require(gameData.has_value(), "Valid game-data folder was rejected.") ||
        !require(gameData->root() == std::filesystem::canonical(root),
                 "Game-data root was not canonicalized.") ||
        !require(gameData->findFile("A2MENU.DAT", error).has_value(),
                 "Existing game-data file was not found.") ||
        !require(!gameData->findFile("missing.dat", error).has_value(),
                 "Missing game-data file was accepted.") ||
        !require(!gameData->findFile("../outside.dat", error).has_value(),
                 "Path escaping the game-data root was accepted.") ||
        !require(!gameData->findFile(outside, error).has_value(),
                 "Absolute game-data path was accepted.")) {
        return 1;
    }

    auto archive = opena2::IndexedArchive::open(*gameData, "TEST.IMG", "TEST.IND", error);
    if (!require(archive.has_value(), "Valid indexed archive was rejected.") ||
        !require(archive->entryCount() == 2, "Indexed archive entry count is incorrect.")) {
        return 1;
    }

    const auto firstEntry = archive->readEntry(0, error);
    const auto lastEntry = archive->readEntry(1, error);
    if (!require(firstEntry.has_value() && firstEntry->size() == 3,
                 "First archive entry was not read correctly.") ||
        !require((*firstEntry)[0] == std::byte{'a'} &&
                     (*firstEntry)[1] == std::byte{'b'} &&
                     (*firstEntry)[2] == std::byte{'c'},
                 "First archive entry contents are incorrect.") ||
        !require(lastEntry.has_value() && lastEntry->size() == 2 &&
                     (*lastEntry)[0] == std::byte{'d'} &&
                     (*lastEntry)[1] == std::byte{'e'},
                 "Last archive entry contents are incorrect.") ||
        !require(!archive->readEntry(2, error).has_value(),
                 "Out-of-range archive entry was accepted.") ||
        !require(!opena2::IndexedArchive::open(
                      *gameData, "BAD.IMG", "BAD.IND", error).has_value(),
                 "Archive with non-monotonic offsets was accepted.") ||
        !require(!opena2::IndexedArchive::open(
                      *gameData, "../outside.dat", "TEST.IND", error).has_value(),
                 "Archive image outside the game-data root was accepted.")) {
        return 1;
    }

    auto namedArchive = opena2::IndexedArchive::openNamed(
        *gameData, "NAMES.IMG", "NAMES.IND", error);
    const auto namedFirstEntry = namedArchive
        ? namedArchive->readEntry(0, error)
        : std::nullopt;
    const auto namedSecondEntry = namedArchive
        ? namedArchive->readEntry(1, error)
        : std::nullopt;
    const auto namedThirdEntry = namedArchive
        ? namedArchive->readEntry(2, error)
        : std::nullopt;
    if (!require(namedArchive.has_value(), "Valid named archive was rejected.") ||
        !require(namedArchive->entryCount() == 3, "Named archive entry count is incorrect.") ||
        !require(namedArchive->entryName(0) == "first.bin" &&
                     namedArchive->entryName(1) == "second.bin" &&
                     namedArchive->entryName(2) == "abcdefghijkl",
                 "Named archive filenames were decoded incorrectly.") ||
        !require(namedFirstEntry && namedSecondEntry && namedThirdEntry &&
                     namedFirstEntry->size() == 3 && namedSecondEntry->size() == 2 &&
                     namedThirdEntry->size() == 2,
                 "Named archive offsets were decoded incorrectly.")) {
        return 1;
    }

    const std::array<std::byte, 12> imageBytes{
        std::byte{2}, std::byte{0}, std::byte{2}, std::byte{0},
        std::byte{0xff}, std::byte{0xff}, std::byte{1}, std::byte{0},
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}};
    const auto indexedImage = opena2::decodeIndexedImage(imageBytes, error);
    const std::array<std::byte, 9> shortImage{};
    if (!require(indexedImage.has_value(), "Valid indexed image was rejected.") ||
        !require(indexedImage->width == 2 && indexedImage->height == 2 &&
                     indexedImage->anchorX == -1 && indexedImage->anchorY == 1,
                 "Indexed image header was decoded incorrectly.") ||
        !require(indexedImage->pixels == std::vector<std::uint8_t>{10, 20, 30, 255},
                 "Indexed image pixel indices were decoded incorrectly.") ||
        !require(!opena2::decodeIndexedImage(
                      std::span<const std::byte>(shortImage).first(7), error).has_value(),
                 "Truncated indexed image header was accepted.") ||
        !require(!opena2::decodeIndexedImage(shortImage, error).has_value(),
                 "Indexed image with invalid pixel length was accepted.")) {
        return 1;
    }

    const auto pcxBytes = makePcx();
    const auto pcxImage = opena2::decodePcx(pcxBytes, error);
    if (!require(pcxImage.has_value(), "Valid indexed PCX image was rejected.") ||
        !require(pcxImage->width == 2 && pcxImage->height == 1,
                 "PCX dimensions were decoded incorrectly.") ||
        !require(pcxImage->pixels == std::vector<std::uint8_t>{10, 20},
                 "PCX RLE pixels were decoded incorrectly.") ||
        !require(pcxImage->paletteRgb.size() == 768 &&
                     pcxImage->paletteRgb[0] == 0 &&
                     pcxImage->paletteRgb[1] == 255 &&
                     pcxImage->paletteRgb[2] == 42,
                 "PCX color palette was decoded incorrectly.")) {
        return 1;
    }
    auto malformedPcx = pcxBytes;
    malformedPcx[0] = std::byte{0};
    auto oversizedPcx = pcxBytes;
    oversizedPcx[10] = std::byte{0xfe};
    oversizedPcx[11] = std::byte{0xff};
    if (!require(!opena2::decodePcx(malformedPcx, error).has_value(),
                 "Invalid PCX header was accepted.") ||
        !require(!opena2::decodePcx(oversizedPcx, error).has_value(),
                 "PCX dimensions exceeding available RLE data were accepted.")) {
        return 1;
    }

    const auto wavBytes = makeWave(1, 22050, 8, {0x80, 0x81, 0x7f, 0xff});
    const auto wave = opena2::decodePcmWave(wavBytes, error);
    if (!require(wave.has_value(), "Valid PCM WAVE was rejected.") ||
        !require(wave->sampleRate == 22050 && wave->channels == 1 &&
                     wave->format == opena2::PcmSampleFormat::unsigned8,
                 "PCM WAVE format was decoded incorrectly.") ||
        !require(wave->samples.size() == 4 &&
                     wave->samples[0] == std::byte{0x80} &&
                     wave->samples[3] == std::byte{0xff},
                 "PCM WAVE samples were decoded incorrectly.")) {
        return 1;
    }

    const auto stereoWaveBytes = makeWave(2, 44100, 16, {0x00, 0x80, 0xff, 0x7f});
    const auto stereoWave = opena2::decodePcmWave(stereoWaveBytes, error);
    const auto lowRateWaveBytes = makeWave(1, 11025, 8, {0x80, 0x90});
    const auto lowRateWave = opena2::decodePcmWave(lowRateWaveBytes, error);
    const auto oddWaveBytes = makeWave(1, 22050, 8, {0x80, 0x81, 0x82});
    const auto oddWave = opena2::decodePcmWave(oddWaveBytes, error);
    auto badWave = wavBytes;
    badWave[0] = std::byte{'X'};
    auto badByteRate = wavBytes;
    badByteRate[28] = std::byte{0};
    if (!require(stereoWave.has_value() && stereoWave->channels == 2 &&
                     stereoWave->format == opena2::PcmSampleFormat::signed16LittleEndian,
                 "16-bit stereo PCM WAVE was rejected.") ||
    !require(lowRateWave.has_value() && lowRateWave->sampleRate == 11025 &&
                 lowRateWave->channels == 1 &&
                 lowRateWave->format == opena2::PcmSampleFormat::unsigned8,
             "11,025 Hz mono PCM WAVE was not preserved.") ||
    !require(oddWave.has_value() && oddWave->samples.size() == 3,
                 "Odd-sized final WAVE data chunk without padding was rejected.") ||
        !require(!opena2::decodePcmWave(badWave, error).has_value(),
                 "Invalid RIFF signature was accepted.") ||
        !require(!opena2::decodePcmWave(badByteRate, error).has_value(),
                 "Invalid PCM byte rate was accepted.")) {
        return 1;
    }

    const auto d3dBytes = makeD3DMesh();
    const auto mesh = opena2::decodeD3DMesh(d3dBytes, error);
    auto truncatedMesh = d3dBytes;
    truncatedMesh.pop_back();
    auto invalidIndexMesh = d3dBytes;
    invalidIndexMesh[invalidIndexMesh.size() - 5] = std::byte{3};
    auto nonFiniteMesh = d3dBytes;
    nonFiniteMesh[8] = std::byte{0};
    nonFiniteMesh[9] = std::byte{0};
    nonFiniteMesh[10] = std::byte{0x80};
    nonFiniteMesh[11] = std::byte{0x7f};
    if (!require(mesh.vertices.size() == 3 && mesh.faces.size() == 1,
                 "Valid D3D mesh counts were decoded incorrectly.") ||
        !require(mesh.vertices[1].position[0] == 1.0F &&
                     mesh.vertices[2].textureCoordinate[1] == 1.0F &&
                     mesh.vertices[0].attribute == 1 &&
                     mesh.faces[0].indices ==
                         std::array<std::uint16_t, 3>{0, 1, 2} &&
                     mesh.faces[0].attributes ==
                         std::array<std::uint8_t, 3>{0, 7, 1},
                 "D3D vertex or face fields were decoded incorrectly.") ||
        !require(opena2::decodeD3DMesh(truncatedMesh, error).vertices.empty() &&
                     !error.empty(),
                 "Truncated D3D mesh was accepted.") ||
        !require(opena2::decodeD3DMesh(invalidIndexMesh, error).vertices.empty() &&
                     !error.empty(),
                 "D3D face with an out-of-range vertex index was accepted.") ||
        !require(opena2::decodeD3DMesh(nonFiniteMesh, error).vertices.empty() &&
                     !error.empty(),
                 "D3D mesh with a non-finite vertex was accepted.")) {
        return 1;
    }

    opena2::VehicleState vehicle;
    for (int i = 0; i < 60; ++i) {
        opena2::updateVehicle(vehicle, {1.0F, 0.0F, 0.0F}, 0.05F);
    }
    if (!require(vehicle.speed > 20.0F && vehicle.speed <= 58.0F &&
                     vehicle.z < 0.0F,
                 "Throttle did not accelerate the vehicle forward.") ||
        !require(vehicle.speed == 58.0F,
                 "Vehicle forward speed did not respect its configured limit.")) {
        return 1;
    }
    const float speedBeforeBrake = vehicle.speed;
    opena2::updateVehicle(vehicle, {0.0F, 1.0F, 0.0F}, 0.05F);
    if (!require(vehicle.speed < speedBeforeBrake && vehicle.speed >= 0.0F,
                 "Brake did not reduce forward speed safely.")) {
        return 1;
    }
    for (int i = 0; i < 20; ++i) {
        opena2::updateVehicle(vehicle, {0.0F, 0.0F, 1.0F}, 0.05F);
    }
    if (!require(vehicle.x > 0.0F && vehicle.heading > 0.0F,
                 "Steering did not turn and move the vehicle.")) {
        return 1;
    }
    for (int i = 0; i < 2000; ++i) {
        opena2::updateVehicle(vehicle, {1.0F, 0.0F, 1.0F}, 0.05F);
    }
    if (!require(vehicle.x <= 26.0F && vehicle.x >= -26.0F,
                 "Vehicle escaped the test surface's lateral bounds.")) {
        return 1;
    }
    const auto stableVehicle = vehicle;
    opena2::updateVehicle(vehicle, {1.0F, 0.0F, 0.0F}, -1.0F);
    if (!require(vehicle.x == stableVehicle.x && vehicle.z == stableVehicle.z &&
                     vehicle.heading == stableVehicle.heading &&
                     vehicle.speed == stableVehicle.speed,
                 "Invalid vehicle time step changed the simulation state.")) {
        return 1;
    }

    const auto emptyRoot = temporary.path() / "empty";
    std::filesystem::create_directories(emptyRoot);
    if (!require(!opena2::GameDataDirectory::open(emptyRoot, error).has_value(),
                 "Folder without A2.EXE was accepted.") ||
        !require(!error.empty(), "Validation failure did not provide an error.")) {
        return 1;
    }

    std::cout << "Game-data directory tests passed.\n";
    return 0;
}
