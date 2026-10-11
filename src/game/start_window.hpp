#pragma once

#include "opena2/installation.hpp"

#include <filesystem>

struct SDL_Window;

namespace opena2::game {

// The start-up steps before the game, drawn in the game window: copying the
// game files from the CD on the first start, and the start window with the
// window options, which replaces the CD's setup program.
struct StartPaths {
    // The preference folder, which holds the rest; emptied on uninstall.
    std::filesystem::path userFolder;
    // Where the game files are copied to.
    std::filesystem::path gameData;
    // launcher.ini.
    std::filesystem::path settings;
    // Development (--capture): save the start window after this many
    // seconds and quit.
    std::filesystem::path capture;
    float captureSeconds = 1.5F;
};

// Makes sure the game files are complete, asking for the CD folder and
// copying them when needed. False when the user gave up or the copy failed.
bool ensureInstalled(SDL_Window* window, LauncherSettings& settings, const StartPaths& paths);

// The start window: window size, render resolution, full screen, the FPS
// counter, reinstalling from a CD folder, opening the game files' folder and
// uninstalling. True for Spelen.
bool runStartWindow(SDL_Window* window, LauncherSettings& settings, const StartPaths& paths);

// Sizes the window for the game: the chosen window size (fitted to the
// screen) or full screen.
void applyWindowSettings(SDL_Window* window, const LauncherSettings& settings);

} // namespace opena2::game
