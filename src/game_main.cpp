#include <SDL3/SDL.h>
// Turns main into the Windows entry point (WinMain).
#include <SDL3/SDL_main.h>

#include "game/display.hpp"
#include "game/gl_functions.hpp"
#include "game/keys.hpp"
#include "game/launcher.hpp"
#include "game/race_program.hpp"
#include "game/menu_assets.hpp"
#include "game/runtime.hpp"
#include "game/sound.hpp"
#include "game/start_window.hpp"
#include "game/task.hpp"

#include "opena2/game_data_directory.hpp"
#include "opena2/installation.hpp"
#include "opena2/screen_transform.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace opena2::game;

// Converts window coordinates into the menu's logical 640x480 space.
std::pair<float, float> toScreenSpace(SDL_Window* window, float windowX, float windowY)
{
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) {
        return {-1.0F, -1.0F};
    }
    return opena2::ScreenTransform::fit(width, height).toLogical(windowX, windowY);
}

// Keeps the old name OpenA2 on purpose: renaming it would lose existing
// installs and saves.
std::filesystem::path preferenceFolder()
{
    char* preferences = SDL_GetPrefPath("OpenA2", "OpenA2");
    if (preferences == nullptr) {
        return {};
    }
    std::filesystem::path path(reinterpret_cast<const char8_t*>(preferences));
    SDL_free(preferences);
    return path;
}

struct RunOptions;
std::optional<opena2::GameDataDirectory> prepareGameData(SDL_Window* window, const RunOptions& options,
                                                         std::string& error);

struct RunOptions {
    std::optional<std::filesystem::path> gameFolder;
    std::optional<std::filesystem::path> capturePath;
    float captureSeconds = 1.5F;
    bool playIntro = true;
    // Start with the last settings, without the start window.
    bool showStartWindow = true;
    // Development: start a race stage, or open it in the free-camera view,
    // instead of the menu.
    std::optional<int> raceStage;
    std::optional<int> raceViewStage;
    int racePiece = 0;
    // Development: with --race, force this race state once racing starts.
    int raceState = -1;
};

// The copy of the CD in the preference folder: installed on the first start,
// then the start window, then the window sized as chosen there.
std::optional<opena2::GameDataDirectory> prepareGameData(SDL_Window* window, const RunOptions& options,
                                                         opena2::LauncherSettings& settings, std::string& error)
{
    const auto folder = preferenceFolder();
    if (folder.empty()) {
        error = std::string("No preference folder: ") + SDL_GetError();
        return std::nullopt;
    }
    StartPaths paths{folder, folder / "game-data", folder / "launcher.ini"};
    if (options.capturePath) {
        paths.capture = *options.capturePath;
        paths.captureSeconds = options.captureSeconds;
    }
    settings = opena2::loadLauncherSettings(paths.settings).value_or(opena2::LauncherSettings{});
    if (!ensureInstalled(window, settings, paths)) {
        return std::nullopt;
    }
    if (options.showStartWindow) {
        if (!runStartWindow(window, settings, paths) || options.capturePath) {
            return std::nullopt;
        }
        opena2::saveLauncherSettings(paths.settings, settings);
    }
    // The game folder may have been deleted while the start window was open.
    if (!ensureInstalled(window, settings, paths)) {
        return std::nullopt;
    }
    applyWindowSettings(window, settings);
    return opena2::GameDataDirectory::open(paths.gameData, error);
}

// Development: sets ETAPPE in INFIO.INI, then runs the race.
Task<> runRaceStage(Runtime& runtime, int stage, int forceState)
{
    opena2::Infio infio = readInfio(runtime);
    infio.stage = static_cast<std::int16_t>(stage);
    infio.raceFinished = 0;
    writeInfio(runtime, infio);
    co_await runRace(runtime, forceState);
}

// SDL_EVENT_TEXT_INPUT gives UTF-8; the original's WM_CHAR is Latin-1.
std::string toLatin1(const char* utf8)
{
    std::string text;
    const auto* byte = reinterpret_cast<const unsigned char*>(utf8);
    while (*byte != '\0') {
        std::uint32_t codepoint = *byte;
        int extra = 0;
        if (codepoint >= 0xF0) {
            codepoint &= 0x07;
            extra = 3;
        } else if (codepoint >= 0xE0) {
            codepoint &= 0x0F;
            extra = 2;
        } else if (codepoint >= 0xC0) {
            codepoint &= 0x1F;
            extra = 1;
        } else if (codepoint >= 0x80) {
            ++byte;
            continue;
        }
        ++byte;
        for (; extra > 0 && (*byte & 0xC0) == 0x80; --extra, ++byte) {
            codepoint = (codepoint << 6) | (*byte & 0x3F);
        }
        if (extra == 0 && codepoint <= 0xFF) {
            text.push_back(static_cast<char>(codepoint));
        }
    }
    return text;
}

void releaseAllKeys(Runtime& runtime)
{
    for (int key = 0; key < 256; ++key) {
        runtime.setKeyDown(key, false);
    }
}

void translateEvent(SDL_Window* window, Runtime& runtime, const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        const auto [x, y] = toScreenSpace(window, event.motion.x, event.motion.y);
        runtime.setMouse(x, y);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event.button.button != SDL_BUTTON_LEFT) {
            break;
        }
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const auto [x, y] = toScreenSpace(window, event.button.x, event.button.y);
        runtime.setMouse(x, y);
        runtime.setKeyDown(vk::leftButton, down);
        InputEvent input;
        input.type = down ? InputEvent::Type::mouseDown : InputEvent::Type::mouseUp;
        input.vk = vk::leftButton;
        const SDL_Keymod modifiers = SDL_GetModState();
        input.alt = (modifiers & SDL_KMOD_ALT) != 0;
        input.shift = (modifiers & SDL_KMOD_SHIFT) != 0;
        input.x = x;
        input.y = y;
        runtime.pushEvent(std::move(input));
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const int key = virtualKey(event.key.key);
        if (key == 0) {
            break;
        }
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        runtime.setKeyDown(key, down);
        InputEvent input;
        input.type = down ? InputEvent::Type::keyDown : InputEvent::Type::keyUp;
        input.vk = key;
        input.repeat = event.key.repeat;
        input.alt = (event.key.mod & SDL_KMOD_ALT) != 0;
        input.shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
        input.x = runtime.mouseX();
        input.y = runtime.mouseY();
        runtime.pushEvent(std::move(input));
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        InputEvent input;
        input.type = InputEvent::Type::text;
        input.text = toLatin1(event.text.text);
        if (!input.text.empty()) {
            runtime.pushEvent(std::move(input));
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        SDL_StartTextInput(window);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Key-up events are not delivered to an inactive window.
        SDL_StopTextInput(window);
        releaseAllKeys(runtime);
        break;
    default:
        break;
    }
}

int runGame(SDL_Window* window, const opena2::GameDataDirectory& directory,
            const MenuAssets& assets, const RunOptions& options, const opena2::LauncherSettings& settings)
{
    std::string error;
    Display display;
    if (!display.initialize(assets, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
        return 1;
    }
    const auto sizes = opena2::windowResolutions();
    if (settings.renderResolution >= 0 && settings.renderResolution < static_cast<int>(sizes.size())) {
        const auto& size = sizes[static_cast<std::size_t>(settings.renderResolution)];
        display.setRenderResolution(size.width, size.height);
    }
    display.setFpsCounter(settings.showFps);
    SoundSystem sound;
    sound.initialize();

    const auto dataFolder = preferenceFolder();
    if (dataFolder.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No folder for saved games: %s", SDL_GetError());
        return 1;
    }
    Runtime runtime(display, sound, assets, directory, dataFolder, window);
    runtime.raceViewDistance = static_cast<float>(settings.viewDistance) / 100.0F;
    runtime.raceTickRate = settings.tickRate;

    // A window started from a terminal, or shown again after the folder
    // picker, is not active on macOS and gets no mouse motion until raised.
    SDL_RaiseWindow(window);
    SDL_StartTextInput(window);
    float mouseX = 0.0F;
    float mouseY = 0.0F;
    SDL_GetMouseState(&mouseX, &mouseY);
    const auto [startX, startY] = toScreenSpace(window, mouseX, mouseY);
    runtime.setMouse(startX, startY);

    Task<> launcher = options.raceViewStage
                          ? runRaceView(runtime, *options.raceViewStage, options.racePiece)
                      : options.raceStage ? runRaceStage(runtime, *options.raceStage, options.raceState)
                                          : runLauncher(runtime, options.playIntro);
    bool started = false;
    const Uint64 startTicks = SDL_GetTicks();
    Uint64 quitTicks = 0;
    while (!launcher.done()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                runtime.quitRequested = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                       (event.key.mod & SDL_KMOD_ALT) != 0) {
                // Alt+Enter switches between full screen and a window.
                SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0);
            } else {
                translateEvent(window, runtime, event);
            }
        }

        const Uint64 now = SDL_GetTicks();
        runtime.setTime(now);
        sound.update();
        if (!started) {
            started = true;
            launcher.start();
        } else {
            runtime.scheduler.resume();
        }

        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        display.render(pixelWidth, pixelHeight);
        if (options.capturePath && !runtime.quitRequested &&
            static_cast<float>(now - startTicks) / 1000.0F > options.captureSeconds) {
            if (!display.capture(*options.capturePath)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not save the capture: %s",
                             SDL_GetError());
            }
            runtime.quitRequested = true;
        }
        SDL_GL_SwapWindow(window);

        // Every loop gives up on quitRequested; allow it two seconds.
        if (runtime.quitRequested) {
            if (quitTicks == 0) {
                quitTicks = now;
            } else if (now - quitTicks > 2000) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The game did not stop in time.");
                break;
            }
        }
        SDL_Delay(1);
    }
    SDL_StopTextInput(window);
    if (launcher.done()) {
        try {
            launcher.result();
        } catch (const std::exception& exception) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", exception.what());
            return 1;
        }
    }
    return 0;
}

} // namespace

// Usage: opena2 [game-folder] [--no-intro] [--no-dialog] [--capture menu.bmp [seconds]]
//               [--race stage [--race-state n]] [--race-view stage [--race-piece n]]
// Without a game folder the game runs from its copy of the CD in the
// preference folder: on the first start it asks for the CD folder and copies
// the files, then the start window offers the window options (--no-dialog
// skips it). A game folder given here is used as it is, without either. --capture saves the
// rendered screen at the window's pixel resolution after `seconds` (default
// 1.5), then exits. Development: --race runs race stage 0..5 with the
// current INFIO.INI and writes its result there (--race-state forces race
// state 3..7 or 9 once racing starts); --race-view opens the stage
// in a free-camera view, starting on road piece n.
int main(int argc, char** argv)
{
    RunOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--capture" && index + 1 < argc) {
            options.capturePath = std::filesystem::path(argv[++index]);
            if (index + 1 < argc) {
                char* end = nullptr;
                const double seconds = SDL_strtod(argv[index + 1], &end);
                if (end != argv[index + 1] && *end == '\0' && seconds > 0.0) {
                    options.captureSeconds = static_cast<float>(seconds);
                    ++index;
                }
            }
        } else if (argument == "--race" && index + 1 < argc) {
            options.raceStage = SDL_atoi(argv[++index]);
        } else if (argument == "--race-view" && index + 1 < argc) {
            options.raceViewStage = SDL_atoi(argv[++index]);
        } else if (argument == "--race-state" && index + 1 < argc) {
            options.raceState = SDL_atoi(argv[++index]);
        } else if (argument == "--race-piece" && index + 1 < argc) {
            options.racePiece = SDL_atoi(argv[++index]);
        } else if (argument == "--no-intro") {
            options.playIntro = false;
        } else if (argument == "--no-dialog") {
            options.showStartWindow = false;
        } else if (argument.starts_with("-psn_")) {
            // Older macOS passes a process serial number to apps opened
            // from the Finder.
        } else {
            options.gameFolder = std::filesystem::path(argument);
        }
    }

    // A click on the inactive window should also press the button under it.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize SDL: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) ||
        !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not configure OpenGL: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "OpenA2Racer", 960, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
            (options.gameFolder ? 0 : SDL_WINDOW_HIDDEN));
    if (window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create the game window: %s",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create OpenGL context: %s",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Vsync paces the frame loop; SDL_Delay below covers drivers without it.
    SDL_GL_SetSwapInterval(1);

    int exitCode = 0;
    std::string error;
    // The start window's choices; the defaults with a game folder given here.
    opena2::LauncherSettings settings;
    if (!gl.load(error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
        exitCode = 1;
    } else if (auto directory = options.gameFolder ? opena2::GameDataDirectory::open(*options.gameFolder, error)
                                                   : prepareGameData(window, options, settings, error)) {
        MenuAssets assets;
        if (!assets.load(*directory, error)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the original menu: %s",
                         error.c_str());
            exitCode = 1;
        } else {
            exitCode = runGame(window, *directory, assets, options, settings);
        }
    } else if (!error.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not open the game folder: %s",
                     error.c_str());
        exitCode = 1;
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
