#include "runtime.hpp"

#include "keys.hpp"

#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <utility>

namespace opena2::game {

Runtime::Runtime(Display& displayRef, SoundSystem& soundRef, const MenuAssets& assetsRef,
                 const GameDataDirectory& directoryRef, std::filesystem::path folder,
                 SDL_Window* windowRef)
    : display(displayRef),
      sound(soundRef),
      assets(assetsRef),
      directory(directoryRef),
      dataFolder(std::move(folder)),
      window(windowRef)
{
}

void Runtime::pushEvent(InputEvent event)
{
    events_.push_back(std::move(event));
}

void Runtime::setKeyDown(int vk, bool down)
{
    if (vk > 0 && vk < static_cast<int>(keys_.size())) {
        keys_[static_cast<std::size_t>(vk)] = down;
    }
}

void Runtime::setMouse(float x, float y)
{
    mouseX_ = x;
    mouseY_ = y;
}

bool Runtime::popEvent(InputEvent& event)
{
    if (events_.empty()) {
        return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

void Runtime::discardEvents()
{
    events_.clear();
}

bool Runtime::keyDown(int vk) const
{
    const int code = vk & 0xFF;
    return code > 0 && keys_[static_cast<std::size_t>(code)];
}

// keyispressed (0x414a3c): GetAsyncKeyState of the key, gated on Shift.
bool Runtime::keyIsPressed(int vk) const
{
    if ((vk & 0x100) != 0) {
        return keyDown(vk::shift) && keyDown(vk & 0xFF);
    }
    return !keyDown(vk::shift) && keyDown(vk);
}

Task<> Runtime::sleep(int milliseconds)
{
    const std::uint64_t start = now_;
    while (!quitRequested && now_ - start < static_cast<std::uint64_t>(std::max(milliseconds, 0))) {
        co_await frame();
    }
}

Task<> Runtime::waitKeyReleased(int vk)
{
    while (!quitRequested && keyDown(vk)) {
        co_await frame();
        // The original pumps messages while it waits, so the key's
        // auto-repeats reach the controls and are lost there. Drop them here
        // so they do not act again once the wait is over.
        std::erase_if(events_, [vk](const InputEvent& event) {
            return event.type == InputEvent::Type::keyDown && event.repeat && event.vk == vk;
        });
    }
}

void Runtime::setMouseEnabled(bool enabled)
{
    mouseEnabled_ = enabled;
    applyCursor();
}

void Runtime::setMouseShown(bool shown)
{
    mouseShown_ = shown;
    applyCursor();
}

void Runtime::applyCursor()
{
    // set_mouse_type(1): the standard arrow, hidden while the mouse is off
    // (WM_SETCURSOR sets no cursor) or hidden (ShowCursor(FALSE)).
    if (mouseEnabled_ && mouseShown_) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

} // namespace opena2::game
