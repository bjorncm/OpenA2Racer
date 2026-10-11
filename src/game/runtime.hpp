#pragma once

#include "display.hpp"
#include "menu_assets.hpp"
#include "sound.hpp"
#include "task.hpp"

#include "opena2/game_data_directory.hpp"

#include <SDL3/SDL_video.h>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>

namespace opena2::game {

// One input event, translated to the Win32 terms the original uses.
struct InputEvent {
    enum class Type { keyDown, keyUp, mouseDown, mouseUp, text };

    Type type = Type::keyDown;
    int vk = 0;            // virtual-key code (keys) or vk::leftButton (mouse)
    bool repeat = false;   // auto-repeated key-down
    bool alt = false;      // Alt held (WM_SYSKEYDOWN)
    bool shift = false;
    float x = 0.0F;        // mouse position in 640x480 screen space
    float y = 0.0F;
    std::string text;      // typed characters (WM_CHAR), Latin-1
};

// Shared services for the ported menu, movie player and launcher. The
// original's blocking loops become coroutines that await `frame()` once per
// loop iteration; the SDL main loop feeds input and resumes the scheduler.
class Runtime {
public:
    Runtime(Display& display, SoundSystem& sound, const MenuAssets& assets,
            const GameDataDirectory& directory, std::filesystem::path dataFolder,
            SDL_Window* window);

    Display& display;
    SoundSystem& sound;
    const MenuAssets& assets;
    const GameDataDirectory& directory;
    // Where the menu's own files (OPTIE.A2R, SLOT.A2R, INFIO.INI...) live.
    std::filesystem::path dataFolder;
    SDL_Window* window;
    FrameScheduler scheduler;

    // --- Fed by the SDL main loop.
    void pushEvent(InputEvent event);
    void setKeyDown(int vk, bool down);
    void setMouse(float x, float y);
    void setTime(std::uint64_t milliseconds) { now_ = milliseconds; }

    // --- Used by the ported code.
    // Takes the next queued input event.
    bool popEvent(InputEvent& event);
    void discardEvents();
    // keyispressed (0x414a3c): `vk` is down and Shift is up; with `vk | 0x100`
    // Shift must be down instead.
    bool keyIsPressed(int vk) const;
    bool keyDown(int vk) const;
    float mouseX() const { return mouseX_; }
    float mouseY() const { return mouseY_; }
    // GetTickCount.
    std::uint64_t now() const { return now_; }

    // Suspends until the next frame (one iteration of an original loop).
    FrameScheduler::Awaiter frame() { return scheduler.nextFrame(); }
    // GetTickCount busy-wait.
    Task<> sleep(int milliseconds);
    // wait_escape/enter/cursor_up/cursor_down_pressed: waits until `vk` is
    // released.
    Task<> waitKeyReleased(int vk);

    // mouse_on/mouse_off and show_mouse/hide_mouse: the Windows arrow cursor.
    void setMouseEnabled(bool enabled);
    void setMouseShown(bool shown);

    // Set when the window is closed; every loop should give up.
    bool quitRequested = false;

    // The start window's race options: the view distance factor (see
    // race::viewWindow) and the race logic's ticks per second.
    float raceViewDistance = 1.0F;
    int raceTickRate = 20;

private:
    void applyCursor();

    std::deque<InputEvent> events_;
    std::array<bool, 256> keys_{};
    float mouseX_ = 0.0F;
    float mouseY_ = 0.0F;
    std::uint64_t now_ = 0;
    bool mouseEnabled_ = true;
    bool mouseShown_ = true;
};

} // namespace opena2::game
