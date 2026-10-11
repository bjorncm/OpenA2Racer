#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opena2 {

// The start window's choices, kept in launcher.ini in the preference folder.
// The window size is the window's size in windowed mode. The render
// resolution limits the size the 640x480 area is drawn at; by default
// (native) the game renders at the window's pixel resolution.
struct WindowResolution {
    int width = 0;
    int height = 0;
    const char* label = "";
};

std::span<const WindowResolution> windowResolutions();

// How far the race draws, in percent of the original: scenery, road and cars
// are drawn inside a box ahead of the camera, which this enlarges.
std::span<const int> viewDistances();
// How many times a second the race logic runs. Everything in the race moves
// a fixed step per tick while the race clock follows real time, so this is
// also the game speed. 20 is the port's default.
std::span<const int> tickRates();

struct LauncherSettings {
    // The window size: an index into windowResolutions().
    int resolution = 1;
    // The render resolution: an index into windowResolutions(), or
    // nativeRender.
    static constexpr int nativeRender = -1;
    int renderResolution = nativeRender;
    bool fullscreen = false;
    bool showFps = false;
    // One of viewDistances() and tickRates().
    int viewDistance = 100;
    int tickRate = 20;
    // The CD folder the game files were copied from.
    std::filesystem::path sourceFolder;

    static LauncherSettings parse(std::string_view text);
    std::string serialize() const;
};

std::optional<LauncherSettings> loadLauncherSettings(const std::filesystem::path& file);
bool saveLauncherSettings(const std::filesystem::path& file, const LauncherSettings& settings);

// The files the game reads; a folder needs all of them to count as the CD.
std::span<const char* const> requiredGameFiles();

// True when `folder` holds the files of the A2 Racer CD-ROM.
bool isGameSourceFolder(const std::filesystem::path& folder);
// True when `gameData` holds a complete copy (with its install.ok marker).
bool isInstalled(const std::filesystem::path& gameData);
// The CD folder recorded in the install.ok marker, or an empty path.
std::filesystem::path installedFrom(const std::filesystem::path& gameData);

// Mounted CD-ROMs and volumes to start the folder picker in; the first one
// that holds the game, or an empty path.
std::filesystem::path findGameCd();

// Uninstalling: deletes what the game keeps in its preference folder `folder`:
// the copied game files and launcher.ini, and with `saves` also the saved
// games, scores and options (with the menu's temp copies). Removes the folder,
// and its parent, when nothing else is left in them. False with `error` when
// something could not be deleted.
bool removeUserData(const std::filesystem::path& folder, bool saves, std::string& error);

struct InstallFile {
    std::filesystem::path source;
    std::filesystem::path name;
    std::uintmax_t size = 0;
};

// What is copied: the files in the root of the CD. Its sub-folders (the
// Helpdesk drivers and DirectX) and the Windows-only files (the DLLs and
// SETUP.EXE) are left out.
std::vector<InstallFile> collectInstallFiles(const std::filesystem::path& source, std::string& error);

// Copies the game files from the CD into the game-data folder. The copy goes
// to <gameData>.tmp first and replaces the folder only when it is complete,
// so a cancelled or failed install never looks installed. run() may be
// called on a worker thread; the progress fields may be read meanwhile.
class Installer {
public:
    Installer(std::filesystem::path source, std::filesystem::path gameData);

    // Copies everything; false on failure or cancel (error() says which).
    bool run();
    void cancel() noexcept { cancelled_ = true; }

    bool cancelled() const noexcept { return cancelled_; }
    std::uintmax_t totalBytes() const noexcept { return total_; }
    std::uintmax_t copiedBytes() const noexcept { return copied_; }
    std::string currentFile() const;
    std::string error() const;

private:
    void setCurrent(std::string name);
    void fail(std::string message);
    bool copyFile(const InstallFile& file, const std::filesystem::path& target);

    std::filesystem::path source_;
    std::filesystem::path gameData_;
    std::atomic<bool> cancelled_ = false;
    std::atomic<std::uintmax_t> total_ = 0;
    std::atomic<std::uintmax_t> copied_ = 0;
    mutable std::mutex mutex_;
    std::string current_;
    std::string error_;
};

} // namespace opena2
