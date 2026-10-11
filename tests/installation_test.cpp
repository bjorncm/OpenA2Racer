#include "opena2/installation.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(fs::temp_directory_path() /
                fs::path("opena2-install-test-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}

std::string readText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// A CD with every game file, the Windows-only files and a driver folder.
fs::path makeCd(const fs::path& root)
{
    fs::create_directories(root / "Helpdesk");
    for (const char* name : opena2::requiredGameFiles()) {
        writeText(root / name, name);
    }
    writeText(root / "LEESMIJ.WRI", "leesmij");
    writeText(root / "SETUP.EXE", "setup");
    writeText(root / "SMACKW32.DLL", "dll");
    writeText(root / "Helpdesk" / "vesasnd.htm", "driver");
    return root;
}

bool testSettings()
{
    bool ok = true;
    const opena2::LauncherSettings defaults;
    ok &= require(defaults.resolution == 1 && !defaults.fullscreen && defaults.sourceFolder.empty() &&
                      defaults.renderResolution == opena2::LauncherSettings::nativeRender && !defaults.showFps &&
                      defaults.viewDistance == 100 && defaults.tickRate == 20,
                  "Launcher defaults: 960 x 720, windowed, native rendering, no FPS counter, no CD folder.");
    opena2::LauncherSettings settings;
    settings.resolution = 3;
    settings.fullscreen = true;
    settings.renderResolution = 0;
    settings.showFps = true;
    settings.viewDistance = 300;
    settings.tickRate = 40;
    settings.sourceFolder = fs::path(u8"/Volumes/A2 Racér");
    const auto text = settings.serialize();
    ok &= require(text.find("RESOLUTION=1920x1440") != std::string::npos, "Resolution is saved as its size.");
    const auto parsed = opena2::LauncherSettings::parse(text);
    ok &= require(parsed.resolution == 3 && parsed.fullscreen && parsed.sourceFolder == settings.sourceFolder &&
                      parsed.renderResolution == 0 && parsed.showFps && parsed.viewDistance == 300 &&
                      parsed.tickRate == 40,
                  "Launcher settings survive a round trip.");
    ok &= require(opena2::LauncherSettings::parse(opena2::LauncherSettings{}.serialize()).renderResolution ==
                      opena2::LauncherSettings::nativeRender,
                  "Native rendering survives a round trip.");
    const auto unknown = opena2::LauncherSettings::parse("RESOLUTION=123x45\nRENDER=1x1\nFULLSCREEN=x\nFPS=x\nVIEWDISTANCE=55\nTICKRATE=0\n");
    ok &= require(unknown.resolution == 1 && !unknown.fullscreen &&
                      unknown.renderResolution == opena2::LauncherSettings::nativeRender && !unknown.showFps &&
                      unknown.viewDistance == 100 && unknown.tickRate == 20,
                  "Unknown values keep the defaults.");
    return ok;
}

bool testSourceFolder(const fs::path& base)
{
    bool ok = true;
    const auto cd = makeCd(base / "cd");
    ok &= require(opena2::isGameSourceFolder(cd), "A complete CD folder is accepted.");
    fs::remove(cd / "RCS.IMG");
    ok &= require(!opena2::isGameSourceFolder(cd), "A CD folder without RCS.IMG is refused.");
    ok &= require(!opena2::isGameSourceFolder({}), "An empty path is refused.");

    const auto cd2 = makeCd(base / "cd2");
    std::string error;
    const auto files = opena2::collectInstallFiles(cd2, error);
    ok &= require(error.empty(), "Collecting the CD files works.");
    ok &= require(files.size() == opena2::requiredGameFiles().size() + 1,
                  "The game files and the readme are copied; nothing else.");
    for (const auto& file : files) {
        const auto name = file.name.string();
        ok &= require(name != "SETUP.EXE" && name != "SMACKW32.DLL" && name != "Helpdesk",
                      "Windows-only files and folders are not copied.");
    }
    return ok;
}

bool testInstall(const fs::path& base)
{
    bool ok = true;
    const auto cd = makeCd(base / "cd");
    const auto gameData = base / "prefs" / "game-data";
    fs::create_directories(base / "prefs");
    writeText(base / "prefs" / "SLOT.A2R", "save");

    ok &= require(!opena2::isInstalled(gameData), "Nothing is installed at first.");
    opena2::Installer installer(cd, gameData);
    ok &= require(installer.run(), "The install succeeds.");
    ok &= require(installer.error().empty(), "The install has no error.");
    ok &= require(opena2::isInstalled(gameData), "The game data counts as installed.");
    ok &= require(opena2::installedFrom(gameData) == cd, "install.ok records the CD folder.");
    ok &= require(installer.copiedBytes() == installer.totalBytes() && installer.totalBytes() > 0,
                  "Every byte is counted.");
    ok &= require(readText(gameData / "RCS.IMG") == "RCS.IMG", "Files are copied with their contents.");
    ok &= require(!fs::exists(gameData / "SETUP.EXE") && !fs::exists(gameData / "Helpdesk"),
                  "Skipped files are not in the game data.");
    ok &= require(!fs::exists(base / "prefs" / "game-data.tmp") && !fs::exists(base / "prefs" / "game-data.old"),
                  "No temporary folders are left.");

    // Reinstalling replaces the folder and leaves the saves beside it alone.
    writeText(gameData / "stale.txt", "old");
    writeText(cd / "RCS.IMG", "new");
    opena2::Installer again(cd, gameData);
    ok &= require(again.run(), "A reinstall succeeds.");
    ok &= require(readText(gameData / "RCS.IMG") == "new" && !fs::exists(gameData / "stale.txt"),
                  "A reinstall replaces the game data.");
    ok &= require(readText(base / "prefs" / "SLOT.A2R") == "save", "Saved games are kept.");

    // A cancelled install leaves the old game data as it was.
    writeText(cd / "RCS.IMG", "cancelled");
    opena2::Installer cancelled(cd, gameData);
    cancelled.cancel();
    ok &= require(!cancelled.run() && cancelled.cancelled(), "A cancelled install fails.");
    ok &= require(readText(gameData / "RCS.IMG") == "new" && opena2::isInstalled(gameData),
                  "A cancelled install keeps the old game data.");
    ok &= require(!fs::exists(base / "prefs" / "game-data.tmp"), "A cancelled install leaves no copy.");

    // An incomplete folder does not count as installed.
    fs::remove(gameData / "A2SPEL.DAT");
    ok &= require(!opena2::isInstalled(gameData), "An incomplete folder is not installed.");
    return ok;
}

bool testRemove(const fs::path& base)
{
    bool ok = true;
    const auto prefs = base / "OpenA2" / "OpenA2";
    const auto fill = [&prefs] {
        fs::create_directories(prefs / "game-data");
        writeText(prefs / "game-data" / "RCS.IMG", "x");
        writeText(prefs / "launcher.ini", "x");
        writeText(prefs / "SLOT.A2R", "save");
        writeText(prefs / "SCORE.TMP", "temp");
    };
    std::string error;
    fill();
    ok &= require(opena2::removeUserData(prefs, false, error) && error.empty(), "Removing the game files works.");
    ok &= require(!fs::exists(prefs / "game-data") && !fs::exists(prefs / "launcher.ini"),
                  "The game files and settings are removed.");
    ok &= require(fs::exists(prefs / "SLOT.A2R") && fs::exists(prefs / "SCORE.TMP"), "The saves are kept.");

    fill();
    writeText(prefs / "notes.txt", "someone else's");
    ok &= require(opena2::removeUserData(prefs, true, error), "Removing everything works.");
    ok &= require(!fs::exists(prefs / "SLOT.A2R") && fs::exists(prefs / "notes.txt"),
                  "The saves go; files the game did not write stay, with their folder.");

    fs::remove(prefs / "notes.txt");
    fill();
    ok &= require(opena2::removeUserData(prefs, true, error), "Removing everything again works.");
    ok &= require(!fs::exists(base / "OpenA2"), "The empty folders are removed too.");
    return ok;
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    bool ok = testSettings();
    ok = testSourceFolder(temporary.path() / "source") && ok;
    ok = testInstall(temporary.path() / "install") && ok;
    ok = testRemove(temporary.path() / "remove") && ok;
    if (ok) {
        std::cout << "Installation tests passed.\n";
    }
    return ok ? 0 : 1;
}
