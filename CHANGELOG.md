# Changelog

OpenA2Racer, an open-source version of the original A2 Racer game from 1997.

## v1.1.0 (2026-10-12)

### Enhancements
- The default name "Speler" in the new-player box starts selected: typing replaces it, Backspace or Delete clears it, and the arrow keys, Home, End or a click keep it for editing. Enter still accepts it as it is.
- The warning that appears when you try to enable software rendering instead of the 3D accelerator is now clearer. 3D acclerator is required for OpenGL.
- Finishing the last stage no longer deletes your player: after the final hiscore the career starts again at the first stage with the money you won, and you return to the main menu. The hiscores stay as before. The maximum amount of money a player can hold is capped at 9999.

### Bugfixes
- MacOS: The start-up dialog looked stretched when transitioning to fullscreen.

## v1.0.0 (2026-10-11)

This list only names what differs from the original game. Everything not listed here is meant to behave as in the original.

### Platform and program

- Fully reworked codebase. Rendering uses OpenGL instead of Direct3D and DirectDraw.
- Runs on macOS and Windows: windowing, input and audio use SDL3 instead of DirectDraw, DirectSound and DirectInput.
- The closed-source Smacker library has been replaced with libsmacker.
- The launcher, menu, movie player and race (A2.EXE, A2MENU.DAT, A2MOVIE.DAT, A2SPEL.DAT) are combined into a single program.
- On the first start the game asks for the folder holding your original game files (the CD or a copy of it) and copies the files it needs into the user's settings folder, so the CD is not needed after that. The original files themselves are never changed. A game folder given as a command-line argument is used as it is, without copying.
- A start window replaces the CD's setup program: it sets the window size, the render resolution (Native by default) and full screen (Alt+Enter switches while playing), turns on an FPS counter, copies the game files again from the CD, opens the folder they were copied to, and uninstalls the game (the copied files, optionally the saved games, and on macOS the app itself). Sizes larger than the screen are not offered.
- On macOS the game is an app (OpenA2Racer.app, with an installer disk image); on Windows a single OpenA2Racer.exe for x86 and ARM64.
- Saved games, options, controls, scores and the race hand-off file (OPTIE.A2R, BEDIEN.A2R, SLOT.A2R, SCORE.A2R, INFIO.INI) keep their original formats, but are stored in the user's settings folder instead of the game folder.
- When a save file cannot be read, the menu keeps its data in memory and goes on instead of stopping the game with a fatal error.
- Closing the window leaves the screen you are on the way its Cancel or Exit button would, also on screens where the original ignored it.
- New command-line options: --no-intro skips the intro movie, --no-dialog skips the start window, and --capture <file.bmp> [seconds] saves a screenshot and exits.
- Development options: --race <stage> starts a race stage straight away (with --race-state <n> putting it into a given race state once racing starts), --race-view <stage> [--race-piece <n>] opens a stage with a free camera, the environment variable OPENA2_RACE_AUTOPILOT=<speed> drives straight ahead at that speed, and OPENA2_FAKE_RACE skips the race with a random finished result.

### Graphics

- The menu keeps its 640x480 layout but is drawn at the window's full resolution, scaled to fit without stretching.
- Menu text is drawn with FreeType at the displayed size, so it stays sharp at any resolution. It uses Arial Black when installed (as the original did), otherwise Arial Bold or DejaVu Sans Bold.
- A lower render resolution can be chosen in the start window: the game is then drawn at that size and scaled up to the window.
- The race's view distance can be raised in the start window, up to 4 times the original: scenery, road and traffic are then drawn further ahead.
- The race's 3D view is drawn at the window's full resolution (or the chosen render resolution); the HUD keeps its 640x480 layout, scaled to fit. Textures keep their 8-bit colours.

### Timing

- The menu's button animations run at a fixed 40 ms per frame; in the original their speed depended on the CPU.
- The race logic runs at a fixed 20 frames per second, a typical rate of a 1997 PC; the original ran it once per drawn frame, so its speed depended on the computer. Drawing still happens at the screen's frame rate. The start window can set another tick rate (10 to 40): everything moves a fixed step per tick while the race clock runs in real time, so a higher rate makes the game faster and the time limits easier.
- Movies never drop frames; after a long stall their clock restarts instead of racing to catch up.

### Controls

- The mouse cursor stays visible while movies play; the original hid it.
- Joysticks and wheels are SDL gamepads. With Joypad selected, the D-pad works as well as the stick.
- F5 (radar on/off) toggles once per key press; the original toggled on every frame the key was held.

### Race

- Race settings: when A2.INI is missing from the game folder, or a key is missing from it, the race uses the values of the A2.INI on the original CD (race times, time bonus, racer and police speeds, the racers' catch-up settings, and the number of police and civilian cars). The original read missing values as 0, so without the file there was no traffic and the computer racers did not move.
- Civilian traffic: one branch of the original's lane-change logic (a civilian in the overtaking lane, at medium distance behind a car that is not slower than it wants to drive, with the lane beside it free behind and taken ahead) reads the speed and position of a car record that does not exist (the entry before the start of the car table, a bug in the original). OpenA2Racer uses the car ahead in that lane instead, so the civilian follows it or moves back next to it.
- Finishing places: the original recorded a car's place at the moment it crossed the line, while the player's car stood still on the line, so cars passing the player could share a place and the stage result showed the wrong names. OpenA2Racer places the cars in the order they crossed the line, then the rest by how far they got. After the finish the HUD and the numbers over the cars keep these places; in the original they kept changing as cars passed the stopped player, ending at 6/6.
- Out-of-range values that made the original read past its tables or stop with a fatal error are clamped instead: the stage number on the route map, and the distance of a car's engine sound at exactly 500 units.
- A new installation starts in 3D Accelerator mode with the dashboard on; the original started in 320 x 200, which has no dashboard. It can still be switched off in Options.