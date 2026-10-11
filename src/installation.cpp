#include "opena2/installation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace opena2 {
namespace {

namespace fs = std::filesystem;

constexpr const char* installMarker = "install.ok";

constexpr std::array<WindowResolution, 5> resolutions{{
    {640, 480, "640 x 480 (origineel)"},
    {960, 720, "960 x 720"},
    {1280, 960, "1280 x 960 (2x)"},
    {1920, 1440, "1920 x 1440 (3x)"},
    {2560, 1920, "2560 x 1920 (4x)"},
}};

constexpr std::array<int, 5> distanceChoices{100, 150, 200, 300, 400};
constexpr std::array<int, 6> tickChoices{10, 15, 20, 25, 30, 40};

// `value` as a number, when it is one of `choices`.
template <std::size_t N>
std::optional<int> choice(std::string_view value, const std::array<int, N>& choices)
{
    for (const int c : choices) {
        if (value == std::to_string(c)) {
            return c;
        }
    }
    return std::nullopt;
}

constexpr std::array<const char*, 22> gameFiles{
    "A2.EXE",      "A2.INI",      "A2INTRO.SMK", "A2WIN.SMK",   "A2EXIT.SMK",  "A2MRC.IMG",
    "A2MRC.IND",   "A2MRCS.IMG",  "A2MRCS.IND",  "RCS.IMG",     "RCS.IND",     "GAMESPR.IMG",
    "GAMESPR.IND", "ETAPPE1.TRK", "ETAPPE1.OMP", "ETAPPE2.TRK", "ETAPPE2.OMP", "ETAPPE3.TRK",
    "ETAPPE3.OMP", "A2MENU.DAT",  "A2SPEL.DAT",  "A2MOVIE.DAT",
};

std::string upper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool skipped(const fs::path& name)
{
    const std::string text = upper(name.filename().string());
    return text == "SETUP.EXE" || (text.size() > 4 && text.ends_with(".DLL"));
}

bool allExist(const fs::path& folder, bool withMarker)
{
    std::error_code error;
    for (const char* name : gameFiles) {
        if (!fs::is_regular_file(folder / name, error)) {
            return false;
        }
    }
    return !withMarker || fs::is_regular_file(folder / installMarker, error);
}

std::string pathText(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string sizeText(const WindowResolution& size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

} // namespace

std::span<const WindowResolution> windowResolutions()
{
    return resolutions;
}

std::span<const int> viewDistances()
{
    return distanceChoices;
}

std::span<const int> tickRates()
{
    return tickChoices;
}

LauncherSettings LauncherSettings::parse(std::string_view text)
{
    LauncherSettings settings;
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = upper(std::string(trim(std::string_view(line).substr(0, equals))));
        const std::string_view value = trim(std::string_view(line).substr(equals + 1));
        if (key == "RESOLUTION" || key == "RENDER") {
            int& target = key == "RESOLUTION" ? settings.resolution : settings.renderResolution;
            for (std::size_t i = 0; i < resolutions.size(); ++i) {
                if (value == sizeText(resolutions[i])) {
                    target = static_cast<int>(i);
                }
            }
        } else if (key == "FULLSCREEN") {
            settings.fullscreen = value == "1";
        } else if (key == "FPS") {
            settings.showFps = value == "1";
        } else if (key == "VIEWDISTANCE") {
            settings.viewDistance = choice(value, distanceChoices).value_or(settings.viewDistance);
        } else if (key == "TICKRATE") {
            settings.tickRate = choice(value, tickChoices).value_or(settings.tickRate);
        } else if (key == "SOURCE") {
            settings.sourceFolder = fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }
    }
    return settings;
}

std::string LauncherSettings::serialize() const
{
    const auto last = static_cast<int>(resolutions.size()) - 1;
    const auto& size = resolutions[static_cast<std::size_t>(std::clamp(resolution, 0, last))];
    const bool native = renderResolution < 0 || renderResolution > last;
    std::ostringstream out;
    out << "[LAUNCHER]\n"
        << "RESOLUTION=" << sizeText(size) << '\n'
        << "RENDER=" << (native ? std::string("native") : sizeText(resolutions[static_cast<std::size_t>(renderResolution)]))
        << '\n'
        << "FULLSCREEN=" << (fullscreen ? 1 : 0) << '\n'
        << "FPS=" << (showFps ? 1 : 0) << '\n'
        << "VIEWDISTANCE=" << viewDistance << '\n'
        << "TICKRATE=" << tickRate << '\n'
        << "SOURCE=" << pathText(sourceFolder) << '\n';
    return out.str();
}

std::optional<LauncherSettings> loadLauncherSettings(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return LauncherSettings::parse(text.str());
}

bool saveLauncherSettings(const fs::path& file, const LauncherSettings& settings)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << settings.serialize();
    return static_cast<bool>(out);
}

std::span<const char* const> requiredGameFiles()
{
    return gameFiles;
}

bool isGameSourceFolder(const fs::path& folder)
{
    return !folder.empty() && allExist(folder, false);
}

bool isInstalled(const fs::path& gameData)
{
    return allExist(gameData, true);
}

bool removeUserData(const fs::path& folder, bool saves, std::string& error)
{
    static constexpr const char* gameFiles[] = {"game-data", "game-data.tmp", "game-data.old", "launcher.ini"};
    static constexpr const char* saveFiles[] = {"OPTIE.A2R", "BEDIEN.A2R", "SLOT.A2R",  "SCORE.A2R",  "INFIO.INI",
                                                "Opties.TMP", "BEDIEN.TMP", "SCORE.TMP", "OPSTART.TMP"};
    std::error_code fsError;
    const auto remove = [&](const char* name) {
        fs::remove_all(folder / name, fsError);
        if (fsError && error.empty()) {
            error = "Kon " + pathText(folder / name) + " niet verwijderen (" + fsError.message() + ").";
        }
        fsError.clear();
    };
    error.clear();
    for (const char* name : gameFiles) {
        remove(name);
    }
    if (saves) {
        for (const char* name : saveFiles) {
            remove(name);
        }
    }
    // fs::remove only deletes empty folders.
    if (fs::remove(folder, fsError)) {
        fs::remove(folder.parent_path(), fsError);
    }
    return error.empty();
}

fs::path installedFrom(const fs::path& gameData)
{
    std::ifstream marker(gameData / installMarker, std::ios::binary);
    std::ostringstream text;
    text << marker.rdbuf();
    const std::string source = text.str();
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(source.data()), source.size()));
}

fs::path findGameCd()
{
    std::vector<fs::path> candidates;
#if defined(_WIN32)
    // An empty CD drive must not ask "There is no disk in the drive".
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        candidates.emplace_back(std::string(1, drive) + ":\\");
    }
#else
    std::vector<fs::path> mountRoots{"/Volumes", "/media", "/mnt"};
    if (const char* user = std::getenv("USER")) {
        mountRoots.emplace_back(fs::path("/media") / user);
        mountRoots.emplace_back(fs::path("/run/media") / user);
    }
    std::error_code error;
    for (const auto& root : mountRoots) {
        for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
            candidates.push_back(it->path());
        }
        error.clear();
    }
#endif
    fs::path found;
    for (const auto& candidate : candidates) {
        if (isGameSourceFolder(candidate)) {
            found = candidate;
            break;
        }
    }
#if defined(_WIN32)
    SetErrorMode(oldMode);
#endif
    return found;
}

std::vector<InstallFile> collectInstallFiles(const fs::path& source, std::string& error)
{
    std::vector<InstallFile> files;
    std::error_code fsError;
    for (fs::directory_iterator it(source, fsError), end; !fsError && it != end; it.increment(fsError)) {
        if (!it->is_regular_file(fsError) || skipped(it->path())) {
            continue;
        }
        const auto size = it->file_size(fsError);
        if (fsError) {
            break;
        }
        files.push_back({it->path(), it->path().filename(), size});
    }
    if (fsError) {
        error = "Kon " + pathText(source) + " niet lezen (" + fsError.message() + ").";
        return {};
    }
    std::sort(files.begin(), files.end(),
              [](const InstallFile& a, const InstallFile& b) { return a.name < b.name; });
    return files;
}

Installer::Installer(fs::path source, fs::path gameData)
    : source_(std::move(source))
    , gameData_(std::move(gameData))
{
}

std::string Installer::currentFile() const
{
    std::lock_guard lock(mutex_);
    return current_;
}

std::string Installer::error() const
{
    std::lock_guard lock(mutex_);
    return error_;
}

void Installer::setCurrent(std::string name)
{
    std::lock_guard lock(mutex_);
    current_ = std::move(name);
}

void Installer::fail(std::string message)
{
    std::lock_guard lock(mutex_);
    error_ = std::move(message);
}

bool Installer::copyFile(const InstallFile& file, const fs::path& target)
{
    std::ifstream in(file.source, std::ios::binary);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        fail("Kon " + pathText(file.source) + " niet kopi\xC3\xABren.");
        return false;
    }
    std::vector<char> buffer(1 << 20);
    while (!cancelled_) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count <= 0) {
            break;
        }
        out.write(buffer.data(), count);
        if (!out) {
            fail("Kon " + pathText(target) + " niet schrijven.");
            return false;
        }
        copied_ += static_cast<std::uintmax_t>(count);
    }
    if (in.bad()) {
        fail("Kon " + pathText(file.source) + " niet kopi\xC3\xABren.");
        return false;
    }
    out.close();
    // Files on a CD are read-only; the copies need not be.
    std::error_code error;
    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add, error);
    return !cancelled_ && static_cast<bool>(out);
}

bool Installer::run()
{
    std::string error;
    const auto files = collectInstallFiles(source_, error);
    if (!error.empty()) {
        fail(error);
        return false;
    }
    std::uintmax_t total = 0;
    for (const auto& file : files) {
        total += file.size;
    }
    total_ = total;

    fs::path temporary = gameData_;
    temporary += ".tmp";
    fs::path old = gameData_;
    old += ".old";
    std::error_code fsError;
    // Left over from an aborted install.
    fs::remove_all(temporary, fsError);
    fs::create_directories(temporary, fsError);
    if (fsError) {
        fail("Kon " + pathText(temporary) + " niet aanmaken (" + fsError.message() + ").");
        return false;
    }
    for (const auto& file : files) {
        if (cancelled_) {
            break;
        }
        setCurrent(pathText(file.name));
        if (!copyFile(file, temporary / file.name)) {
            break;
        }
    }
    if (cancelled_ || !this->error().empty()) {
        fs::remove_all(temporary, fsError);
        return false;
    }

    // Mark complete, then swap the folders.
    {
        std::ofstream marker(temporary / installMarker, std::ios::binary | std::ios::trunc);
        marker << pathText(source_);
    }
    fs::remove_all(old, fsError);
    fsError.clear();
    if (fs::exists(gameData_, fsError)) {
        fs::rename(gameData_, old, fsError);
        if (fsError) {
            fail("Kon de oude map " + pathText(gameData_) + " niet vervangen (" + fsError.message() +
                 "). Is het spel nog geopend?");
            fs::remove_all(temporary, fsError);
            return false;
        }
    }
    fs::rename(temporary, gameData_, fsError);
    if (fsError) {
        fail("Kon " + pathText(gameData_) + " niet aanmaken (" + fsError.message() + ").");
        std::error_code ignored;
        fs::rename(old, gameData_, ignored);
        fs::remove_all(temporary, ignored);
        return false;
    }
    fs::remove_all(old, fsError);
    setCurrent({});
    return true;
}

} // namespace opena2
