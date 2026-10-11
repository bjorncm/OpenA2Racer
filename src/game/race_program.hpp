#pragma once

#include "runtime.hpp"
#include "task.hpp"

namespace opena2::game {

// Development view of a race stage (0..5): the road, the scenery and the sky
// as A2SPEL.DAT draws them, seen from a free camera. Arrow keys move and
// turn, A and Z raise and lower, Home and End tilt, Shift is faster, Esc
// leaves. `startPiece` is the road piece the camera starts on.
Task<> runRaceView(Runtime& runtime, int stage, int startPiece);

// A2SPEL.DAT: the race of the stage INFIO.INI names. Runs the start-up, the
// fly-in and start lights, the race until it ends (finish, Esc, time, fuel
// or money out), then writes the result to INFIO.INI. The HUD and the
// sounds follow the INFIO options.
//
// Development: `forceState` 3..7 or 9 puts the race into that state once it
// starts racing, through the same rule the game uses (the finish, a police
// stop, the clock, money or fuel running out, pause), to check the overlays
// and sounds of each state.
Task<> runRace(Runtime& runtime, int forceState = -1);

} // namespace opena2::game
