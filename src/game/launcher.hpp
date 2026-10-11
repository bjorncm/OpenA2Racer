#pragma once

#include "runtime.hpp"
#include "task.hpp"

#include "opena2/menu_data.hpp"

namespace opena2::game {

// A2.EXE ("A2 MOTOR"): plays the intro movie, then loops the menu program
// (with /FIRST the first time) and the race while the menu asks for a race
// (exit code 0x28), plays the win movie after the last stage, and finally
// the exit movie.
Task<> runLauncher(Runtime& runtime, bool playIntro);

// A2MOVIE.DAT: plays a Smacker movie full screen. Esc, Enter, Space or the
// left mouse button (with Shift up) skips it; it ends on its last frame.
Task<> playMovie(Runtime& runtime, const char* fileName);

// INFIO.INI in the data folder. A missing file reads as all zeros, the way
// GetPrivateProfileInt reads it.
Infio readInfio(const Runtime& runtime);
bool writeInfio(const Runtime& runtime, const Infio& infio);

} // namespace opena2::game
