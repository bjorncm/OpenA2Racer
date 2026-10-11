#pragma once

#include <SDL3/SDL_keycode.h>

#include <cstdint>

namespace opena2::game {

// Win32 virtual-key codes, which the original menu uses for hot keys and
// stores in BEDIEN.A2R.
namespace vk {
constexpr int backspace = 0x08;
constexpr int tab = 0x09;
constexpr int enter = 0x0D;
constexpr int shift = 0x10;
constexpr int control = 0x11;
constexpr int alt = 0x12;
constexpr int escape = 0x1B;
constexpr int space = 0x20;
constexpr int end = 0x23;
constexpr int home = 0x24;
constexpr int left = 0x25;
constexpr int up = 0x26;
constexpr int right = 0x27;
constexpr int down = 0x28;
constexpr int del = 0x2E;
constexpr int numpad0 = 0x60;
constexpr int f1 = 0x70;
constexpr int f2 = 0x71;
constexpr int f5 = 0x74;
constexpr int f8 = 0x77;
constexpr int f12 = 0x7B;
constexpr int leftButton = 0x01;
} // namespace vk

// Virtual-key code for an SDL key, or 0 when it has none.
int virtualKey(SDL_Keycode key);

} // namespace opena2::game
