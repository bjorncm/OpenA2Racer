#include "keys.hpp"

namespace opena2::game {

int virtualKey(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) {
        return 'A' + static_cast<int>(key - SDLK_A);
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return '0' + static_cast<int>(key - SDLK_0);
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return vk::numpad0 + 1 + static_cast<int>(key - SDLK_KP_1);
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return 0x70 + static_cast<int>(key - SDLK_F1);
    }
    switch (key) {
    case SDLK_KP_0: return vk::numpad0;
    case SDLK_BACKSPACE: return vk::backspace;
    case SDLK_TAB: return vk::tab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return vk::enter;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return vk::shift;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return vk::control;
    case SDLK_LALT:
    case SDLK_RALT: return vk::alt;
    case SDLK_ESCAPE: return vk::escape;
    case SDLK_SPACE: return vk::space;
    case SDLK_END: return vk::end;
    case SDLK_HOME: return vk::home;
    case SDLK_LEFT: return vk::left;
    case SDLK_UP: return vk::up;
    case SDLK_RIGHT: return vk::right;
    case SDLK_DOWN: return vk::down;
    case SDLK_DELETE: return vk::del;
    default: return 0;
    }
}

} // namespace opena2::game
