#include "launcher.hpp"

#include "keys.hpp"
#include "menu_program.hpp"
#include "race_program.hpp"

#include "opena2/menu_data.hpp"
#include "opena2/smacker_movie.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace opena2::game {
namespace {

constexpr const char* infioFile = "INFIO.INI";

std::filesystem::path infioPath(const Runtime& runtime)
{
    return runtime.dataFolder / infioFile;
}

} // namespace

Infio readInfio(const Runtime& runtime)
{
    const auto bytes = readFileBytes(infioPath(runtime));
    if (!bytes) {
        // GetPrivateProfileInt on a missing file gives 0 for every key.
        Infio missing;
        missing.raceFinished = 0;
        missing.stage = 0;
        missing.position = 0;
        return missing;
    }
    return Infio::parse(std::string(bytes->begin(), bytes->end()));
}

bool writeInfio(const Runtime& runtime, const Infio& infio)
{
    const std::string text = infio.serialize();
    if (!writeFileBytes(infioPath(runtime),
                        {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()})) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s.", infioFile);
        return false;
    }
    return true;
}

namespace {

// A new process gets fresh DirectDraw surfaces; between modules the
// launcher's black full-screen window shows.
void clearSurfaces(Display& display)
{
    display.clear(Surface::front);
    display.clear(Surface::back);
    display.clear(Surface::buffer);
}

// find_file (A2MOVIE 0x402c09): the movie in the game folder.
std::optional<std::filesystem::path> findMovie(const Runtime& runtime, const char* fileName)
{
    std::string error;
    if (auto path = runtime.directory.findFile(fileName, error)) {
        return path;
    }
    std::string lower(fileName);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (auto path = runtime.directory.findFile(lower, error)) {
        return path;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Movie %s not found: %s", fileName, error.c_str());
    return std::nullopt;
}

// poll_keys (A2MOVIE 0x4060fc) with the keys init_smacker (0x405d98)
// registers: key_down (0x40a8cc) requires Shift to be up.
bool skipPressed(const Runtime& runtime)
{
    return runtime.keyIsPressed(vk::escape) || runtime.keyIsPressed(vk::enter) ||
           runtime.keyIsPressed(vk::space) || runtime.keyIsPressed(vk::leftButton);
}

// Development stand-in for a finished race: what A2SPEL writes at race end
// (0x401c30..0x401d08) with random but consistent results.
void writeFakeRace(const Runtime& runtime)
{
    Infio infio = readInfio(runtime);
    std::random_device device;
    std::mt19937 random(device());

    // Six distinct finishing times between 500 and 900 s, fastest first.
    std::array<std::int32_t, 6> times{};
    std::uniform_int_distribution<std::int32_t> timeRange(500, 900);
    for (std::size_t i = 0; i < times.size(); ++i) {
        std::int32_t candidate = 0;
        do {
            candidate = timeRange(random);
        } while (std::find(times.begin(), times.begin() + static_cast<std::ptrdiff_t>(i),
                           candidate) != times.begin() + static_cast<std::ptrdiff_t>(i));
        times[i] = candidate;
    }
    std::sort(times.begin(), times.end());

    // positions[0] is the player, positions[1..5] the computer racers.
    std::array<std::int16_t, 6> positions{};
    std::iota(positions.begin(), positions.end(), std::int16_t{1});
    std::shuffle(positions.begin(), positions.end(), random);

    infio.raceFinished = 1;
    infio.position = positions[0];
    infio.time = times[static_cast<std::size_t>(positions[0] - 1)];
    for (std::size_t racer = 0; racer < infio.racerPosition.size(); ++racer) {
        infio.racerPosition[racer] = positions[racer + 1];
        infio.racerTime[racer] = times[static_cast<std::size_t>(positions[racer + 1] - 1)];
    }

    if (!writeInfio(runtime, infio)) {
        return;
    }
    SDL_Log("OPENA2_FAKE_RACE: stage %d finished in position %d, %d s.", infio.stage + 1,
            infio.position, infio.time);
}

} // namespace

// A2.EXE WinMain (0x401167).
Task<> runLauncher(Runtime& runtime, bool playIntro)
{
    // run_module("a2movie.dat", "a2intro.smk") at 0x4012ae.
    if (playIntro) {
        clearSurfaces(runtime.display);
        co_await playMovie(runtime, "A2INTRO.SMK");
    }
    // "/FIRST" is passed only on the first pass (cleared at 0x4013b6).
    bool first = true;
    while (!runtime.quitRequested) {
        clearSurfaces(runtime.display);
        int result = 0;
        {
            MenuProgram menu(runtime, first);
            result = co_await menu.run();
        }
        first = false;
        if (runtime.quitRequested) {
            co_return;
        }
        if (result != menu_result::startRace) {
            break;
        }
        clearSurfaces(runtime.display);
        // Development only: OPENA2_FAKE_RACE writes a random finished result
        // instead of running the race, to reach the post-race screens fast.
        if (SDL_getenv("OPENA2_FAKE_RACE") != nullptr) {
            writeFakeRace(runtime);
        } else {
            co_await runRace(runtime);
        }
        if (runtime.quitRequested) {
            co_return;
        }
        // 0x401381..0x40138c: in practice "stage 6 was finished".
        const Infio infio = readInfio(runtime);
        if (infio.stage == 5 && infio.raceFinished != 0 && infio.position != 0) {
            clearSurfaces(runtime.display);
            co_await playMovie(runtime, "A2WIN.SMK");
        }
    }
    if (runtime.quitRequested) {
        co_return;
    }
    // WM_CLOSE -> WM_QUIT -> run_module("a2movie.dat", "a2exit.smk") at 0x4013dc.
    clearSurfaces(runtime.display);
    co_await playMovie(runtime, "A2EXIT.SMK");
}

// A2MOVIE.DAT WinMain (0x40109d) and play_smacker (0x406225 -> 0x406006).
Task<> playMovie(Runtime& runtime, const char* fileName)
{
    const auto path = findMovie(runtime, fileName);
    if (!path) {
        // cd_check (0x402b60) -> fatal error (0x401642): a modal "Fout" box,
        // then the player exits and the launcher continues.
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Fout",
            "Incorrect cdrom was found: please insert the correct cdrom and restart the program",
            runtime.window);
        runtime.discardEvents();
        co_return;
    }
    std::string error;
    auto movie = SmackerMovie::open(*path, error);
    if (!movie) {
        // "failed to open smacker file(%s)": logged, no error box.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open %s: %s", fileName,
                    error.c_str());
        co_return;
    }

    // The original hides the cursor here (init_mouse(); mouse_off(),
    // 0x402d18 / 0x402e3e). OpenA2Racer keeps it visible, so a click to skip the
    // movie has something to point with (see changelog.txt).
    runtime.setMouseEnabled(true);
    runtime.setMouseShown(true);
    runtime.discardEvents();
    if (movie->hasAudio()) {
        runtime.sound.startStream(static_cast<int>(movie->audioRate()), movie->audioChannels(),
                                  movie->audioBits());
        runtime.sound.queueStream(movie->audio());
    }

    const auto frameMilliseconds = movie->frameSeconds() * 1000.0;
    double deadline = static_cast<double>(runtime.now());
    // The image on the front buffer: frame k is flipped only after the wait
    // for frame k+1, so each frame shows one frame time late.
    std::vector<std::uint8_t> shown;
    for (unsigned long frame = 0;; ++frame) {
        // upload_palette (0x405e36) runs as soon as frame k is decoded and is
        // applied at once, so during the wait the previous image already
        // shows with the new palette.
        if (frame > 0) {
            runtime.display.showMovieFrame(shown, movie->width(), movie->height(),
                                           movie->paletteRgb());
        }
        // next_and_wait (0x405ec5): the last frame is flipped without a wait.
        if (frame + 1 >= movie->frameCount()) {
            runtime.display.showMovieFrame(movie->pixels(), movie->width(), movie->height(),
                                           movie->paletteRgb());
            co_await runtime.frame();
            break;
        }
        // SmackWait pacing: frames are never dropped, so a slow machine plays
        // the movie slower. After a long stall the clock restarts instead of
        // racing to catch up. [guess: SmackWait's internals are not decoded]
        deadline += frameMilliseconds;
        const auto now = static_cast<double>(runtime.now());
        if (now > deadline + 4.0 * frameMilliseconds) {
            deadline = now;
        }
        // Keys are polled at least once per frame, from frame 0 on.
        bool skip = false;
        do {
            skip = skipPressed(runtime);
            co_await runtime.frame();
            runtime.discardEvents();
        } while (!skip && !runtime.quitRequested &&
                 static_cast<double>(runtime.now()) < deadline);
        if (runtime.quitRequested) {
            break;
        }
        // screen_update (0x4016e7): Flip shows frame k; a skipped movie still
        // flips its current frame.
        runtime.display.showMovieFrame(movie->pixels(), movie->width(), movie->height(),
                                       movie->paletteRgb());
        if (skip) {
            co_await runtime.frame();
            break;
        }
        shown.assign(movie->pixels().begin(), movie->pixels().end());
        if (!movie->next()) {
            co_await runtime.frame();
            break;
        }
        if (movie->hasAudio()) {
            runtime.sound.queueStream(movie->audio());
        }
    }

    // SmackClose stops the audio; close_xvgalib tears the player down.
    runtime.sound.stopStream();
    runtime.display.hideMovie();
    runtime.discardEvents();
    runtime.setMouseEnabled(true);
    runtime.setMouseShown(true);
}

} // namespace opena2::game
