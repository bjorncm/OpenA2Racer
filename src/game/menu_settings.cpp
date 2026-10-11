// m_opties.cpp and m_bedien.cpp of A2MENU.DAT: the Opties and Bediening
// screens.

#include "menu_program.hpp"

#include "anims.hpp"
#include "display.hpp"
#include "keys.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace opena2::game {
namespace {

// Each step of the dialog and slide-in animations is held for at least this
// long (GetTickCount difference < 0x21).
constexpr std::uint64_t animationStepMs = 33;

// Music volume while help plays (0x406793, 0x40742b).
constexpr int helpMusicVolume = -8000;

// mm_tune (0x432fb4): menu music by slot.level.
constexpr std::array<std::string_view, 6> menuTunes{
    "speed.wav", "energy.wav", "jammin.wav", "moody.wav", "disco.wav", "speed2.wav"};

std::string_view menuTune(int stage)
{
    // The original indexes the table unchecked; a finished player (stage 6)
    // would read past it. [guess] clamp to the table.
    return menuTunes[static_cast<std::size_t>(std::clamp(stage, 0, 5))];
}

// --- m_bedien.cpp ------------------------------------------------------------

// mc_help (0x4335a0) with a joystick, mc_help2 (0x4335d0) without.
constexpr HelpEntry mcHelp[] = {
    {0, "MC_00.wav"}, {1, "MC_01.wav"}, {3, "MC_03.wav"}, {4, "MC_04.wav"}, {5, "MC_02.wav"}};
constexpr HelpEntry mcHelp2[] = {{0, "MC_00.wav"}, {1, "MC_01.wav"}, {3, "MC_03.wav"}};

// mc_instel_wav (0x4337d8): prompts for LINKS, RECHTS, GAS, REM.
constexpr std::array<const char*, 4> mcInstelWav{"MC_05.wav", "MC_06.wav", "MC_07.wav", "MC_08.wav"};

// config_key (0x433748): x1, y1, x2, y2 and name of each key box. Only the
// first four are used; TOETER (477,212) and AKTIE (303,212) are unused.
struct KeyBox {
    int x1;
    int y1;
    int x2;
    int y2;
    const char* name;
};
constexpr std::array<KeyBox, 4> keyBoxRects{{
    {359, 181, 455, 215, "LINKS"},
    {492, 181, 588, 215, "RECHTS"},
    {492, 291, 588, 325, "GAS"},
    {359, 291, 455, 325, "REM"},
}};

// key_list (0x4335f0): the keys mc_wait_key accepts and their names.
struct KeyName {
    int vk;
    const char* name;
};
constexpr std::array<KeyName, 43> keyList{{
    {-1, "   "},
    {0x41, " A "}, {0x42, " B "}, {0x43, " C "}, {0x44, " D "}, {0x45, " E "},
    {0x46, " F "}, {0x47, " G "}, {0x48, " H "}, {0x49, " I "}, {0x4A, " J "},
    {0x4B, " K "}, {0x4C, " L "}, {0x4D, " M "}, {0x4E, " N "}, {0x4F, " O "},
    {0x50, " P "}, {0x51, " Q "}, {0x52, " R "}, {0x53, " S "}, {0x54, " T "},
    {0x55, " U "}, {0x56, " V "}, {0x57, " W "}, {0x58, " X "}, {0x59, " Y "},
    {0x5A, " Z "},
    {0x60, "NUMPAD 0"}, {0x61, "NUMPAD 1"}, {0x62, "NUMPAD 2"}, {0x63, "NUMPAD 3"},
    {0x64, "NUMPAD 4"}, {0x65, "NUMPAD 5"}, {0x66, "NUMPAD 6"}, {0x67, "NUMPAD 7"},
    {0x68, "NUMPAD 8"}, {0x69, "NUMPAD 9"},
    {0x26, "Omhoog"}, {0x28, "Omlaag"}, {0x27, "Rechts"}, {0x25, "Links"},
    {0x20, "SPACE"}, {0x0D, "ENTER"},
}};

// scan_2_ckey (0x405c0e): entry 0 ("   ") when the key is not in the list.
const char* keyName(int vk)
{
    for (const auto& entry : keyList) {
        if (entry.vk == vk) return entry.name;
    }
    return keyList[0].name;
}

// "%-8s"
std::string padRight8(std::string_view text)
{
    std::string result(text);
    if (result.size() < 8) result.append(8 - result.size(), ' ');
    return result;
}

// mc_animate_buttons (0x4337e8/0x4337f8) and mo_animate_buttons
// (0x434090/0x4340a0): frames of the OK and Annuleren slide-in anims.
constexpr std::array<int, 8> slideOkFrames{1, 2, 3, 4, 5, 6, 7, 7};
constexpr std::array<int, 8> slideCancelFrames{0, 1, 2, 3, 4, 5, 6, 7};

// Dialog of the Instellen flow: anim 39 at (23,121), 247x105.
constexpr int dialogAnim = 39;
constexpr int dialogX = 23;
constexpr int dialogY = 121;
constexpr int dialogRight = 270;
constexpr int dialogBottom = 226;

constexpr int keyboardForm = 0;
constexpr int joystickDevice = 1;
constexpr int wheelDevice = 2;

// --- m_opties.cpp ------------------------------------------------------------

// mo_help (0x434060).
constexpr HelpEntry moHelp[] = {
    {0, "MO_00.wav"}, {1, "MO_01.wav"}, {3, "MO_04.wav"}, {4, "MO_02.wav"}, {5, "MO_03.wav"}};

// The bullet of every option value: frame 1 when selected.
constexpr int bulletAnim = 59;

// Writes the form table's state field (+0x10), which activate_form applies.
void setTableState(FormDef& form, std::size_t index, int value)
{
    if (index >= form.controls.size()) return;
    form.controls[index].initialState = value;
    form.controls[index].stateValue = value;
}

} // namespace

// =============================================================================
// Bediening (m_bedien.cpp)
// =============================================================================

// mc_dialog_printf (0x405b39): the full dialog frame and wrapped text, drawn
// on the work buffer and copied to the screen.
void MenuProgram::mcDialogPrint(std::string_view text)
{
    Display& display = runtime_.display;
    display.sprite(Surface::back, animSprite(dialogAnim, 7), dialogX, dialogY);
    printWrapped(Surface::back, font::controls, colorRef(0x5A32FF), 43, 141, 200, text);
    display.copy(Surface::back, Surface::front, dialogX, dialogY, dialogRight, dialogBottom);
}

// print_keys (0x405c96)
void MenuProgram::mcDrawKeyBoxes()
{
    Display& display = runtime_.display;
    for (std::size_t i = 0; i < keyBoxRects.size(); ++i) {
        const KeyBox& box = keyBoxRects[i];
        display.sprite(Surface::front, animSprite(52, 0), box.x1 - 8, box.y1 - 8);
        print(Surface::front, font::controls, colorRef(0xFAF0FF), box.x1, box.y1,
              padRight8(keyName(keyBoxes_[i])));
    }
}

// clear_keys (0x405dac): the full dialog frame (mc_clear_text_bar 0x405afa)
// and the four boxes without text.
void MenuProgram::mcBlankKeyBoxes()
{
    Display& display = runtime_.display;
    display.sprite(Surface::front, animSprite(dialogAnim, 7), dialogX, dialogY);
    for (const KeyBox& box : keyBoxRects) {
        display.sprite(Surface::front, animSprite(52, 0), box.x1 - 8, box.y1 - 8);
    }
}

// mb_read_key (0x405eb2): polls until Esc or a key of key_list is down, waits
// for its release and returns it.
Task<int> MenuProgram::mcWaitKey()
{
    auto waitReleased = [this](int key) -> Task<> {
        while (runtime_.keyIsPressed(key) && !runtime_.quitRequested) {
            co_await runtime_.frame();
            runtime_.discardEvents();
        }
    };

    for (;;) {
        co_await runtime_.frame();
        // No form is active: the window procedure swallows the key messages.
        runtime_.discardEvents();
        if (runtime_.quitRequested) co_return vk::escape;
        if (runtime_.keyIsPressed(vk::escape)) {
            co_await waitReleased(vk::escape);
            co_return vk::escape;
        }
        for (std::size_t i = 1; i < keyList.size(); ++i) {
            const int key = keyList[i].vk;
            if (runtime_.keyIsPressed(key)) {
                co_await waitReleased(key);
                co_return key;
            }
        }
    }
}

// mc_animate_open_textbox (0x405f26)
Task<> MenuProgram::mcDialogOpen()
{
    runtime_.setMouseShown(false);
    for (int frame = 0; frame < 8; ++frame) {
        const std::uint64_t start = runtime_.now();
        runtime_.display.sprite(Surface::front, animSprite(dialogAnim, frame), dialogX, dialogY);
        do {
            co_await runtime_.frame();
            runtime_.discardEvents();
        } while (runtime_.now() - start < animationStepMs && !runtime_.quitRequested);
    }
    runtime_.setMouseShown(true);
}

// mc_animate_close_textbox (0x405f8d)
Task<> MenuProgram::mcDialogClose()
{
    runtime_.setMouseShown(false);
    for (int frame = 7; frame >= 0; --frame) {
        const std::uint64_t start = runtime_.now();
        runtime_.display.sprite(Surface::front, animSprite(dialogAnim, frame), dialogX, dialogY);
        do {
            co_await runtime_.frame();
            runtime_.discardEvents();
        } while (runtime_.now() - start < animationStepMs && !runtime_.quitRequested);
    }
    runtime_.setMouseShown(true);
}

// config_keys (0x405fec): assigns LINKS, RECHTS, GAS and REM in turn. The new
// keys stay in the box table; returns false when cancelled with Esc.
Task<bool> MenuProgram::mcInstellen()
{
    int voice = SoundSystem::none;
    int click = SoundSystem::none;

    co_await mcDialogOpen();
    sndSetVolume(musicHandle_, duckedMusicVolume());
    keyBoxes_.fill(-1); // clear_all_ckey 0x405e2a

    std::size_t index = 0;
    mcDrawKeyBoxes();
    sndPlay(voice, mcInstelWav[index]);
    for (;;) {
        co_await runtime_.frame();
        mcDialogPrint(std::string("Kies nu een toets voor:\n") + " " + keyBoxRects[index].name);
        const int key = co_await mcWaitKey();
        if (key == vk::escape) {
            sndStop(click);
            sndPlay(click, "click1.wav");
            // load_ckey 0x405c35: revert all four boxes.
            for (std::size_t i = 0; i < keyBoxes_.size(); ++i) keyBoxes_[i] = controls_.keys[i];
            mcDrawKeyBoxes();
            co_await mcDialogClose();
            co_await sndWait(click);
            sndSetVolume(musicHandle_, musicVolume());
            co_return false;
        }

        sndPlay(click, "click3.wav");
        // ckey_new_define 0x405e7f: only keys set during this run clash.
        // (ckey_defined 0x405e57 is called too, but its result is unused.)
        if (std::find(keyBoxes_.begin(), keyBoxes_.end(), key) != keyBoxes_.end()) {
            mcBlankKeyBoxes();
            sndPlay(voice, "MC_09.wav");
            co_await reqWarning(std::string("Deze toets, <") + keyName(key) +
                                ">, is eerder gedefinieerd. Kies een andere toets.");
            sndStop(voice);
            mcDrawKeyBoxes();
            continue;
        }

        keyBoxes_[index] = key;
        ++index;
        mcDrawKeyBoxes();
        if (index >= keyBoxes_.size()) break;
        mcDrawKeyBoxes();
        sndPlay(voice, mcInstelWav[index]);
    }

    sndPlay(voice, "MC_10.wav");
    mcDialogPrint("De toetsen zijn ingesteld.");
    co_await sndWait(voice);
    co_await mcDialogClose();
    sndSetVolume(musicHandle_, musicVolume());
    co_return true;
}

// display_keys (0x4061fb): sprite 236 shows only the keyboard tab when no
// joystick is attached.
void MenuProgram::mcDrawKeyboardPanel()
{
    const int sprite = animSprite(51, joystickAvailable_ ? 0 : 3);
    runtime_.display.sprite(Surface::buffer, sprite, 306, 125);
    runtime_.display.sprite(Surface::front, sprite, 306, 125);
    mcDrawKeyBoxes();
}

// display_joystick (0x40629a)
void MenuProgram::mcDrawJoystickPanel()
{
    const int sprite = animSprite(51, 1);
    runtime_.display.sprite(Surface::buffer, sprite, 306, 125);
    runtime_.display.sprite(Surface::front, sprite, 306, 125);
}

// display_stuur (0x40631e)
void MenuProgram::mcDrawWheelPanel()
{
    const int sprite = animSprite(51, 2);
    runtime_.display.sprite(Surface::buffer, sprite, 306, 125);
    runtime_.display.sprite(Surface::front, sprite, 306, 125);
}

// mc_display_controls (0x406363): draws the panel of the device and patches
// the tab states in the form tables.
void MenuProgram::mcInitDevice()
{
    if (!joystickAvailable_) controls_.device = keyboardForm;
    switch (controls_.device) {
    case keyboardForm: {
        mcDrawKeyboardPanel();
        const int tabState = joystickAvailable_ ? state::enable : state::normal;
        setTableState(formTables_.controlsKeyboard, 4, tabState);
        setTableState(formTables_.controlsKeyboard, 5, tabState);
        break;
    }
    case joystickDevice:
        mcDrawJoystickPanel();
        for (std::size_t i = 3; i <= 5; ++i) setTableState(formTables_.controlsJoystick, i, state::enable);
        break;
    case wheelDevice:
        mcDrawWheelPanel();
        for (std::size_t i = 3; i <= 5; ++i) setTableState(formTables_.controlsJoystick, i, state::enable);
        break;
    default:
        break;
    }
}

// mc_animate_buttons (0x4063f8): OK and Annuleren slide in.
Task<> MenuProgram::mcSlideIn()
{
    runtime_.setMouseShown(false);
    for (std::size_t step = 0; step < slideOkFrames.size(); ++step) {
        const std::uint64_t start = runtime_.now();
        runtime_.display.sprite(Surface::front, animSprite(42, slideOkFrames[step]), 0, 331);
        runtime_.display.sprite(Surface::front, animSprite(43, slideCancelFrames[step]), 0, 382);
        do {
            co_await runtime_.frame();
            runtime_.discardEvents();
        } while (runtime_.now() - start < animationStepMs && !runtime_.quitRequested);
    }
    runtime_.setMouseShown(true);
}

void MenuProgram::mcCreateForm()
{
    forms_.activate(controls_.device == keyboardForm ? formTables_.controlsKeyboard
                                                     : formTables_.controlsJoystick);
}

// mc_open_screen (0x406491)
Task<> MenuProgram::mcOpenScreen()
{
    loadScreen("BEDIEN.PCX", true);
    runtime_.setMouseShown(false);
    co_await wipe();
    // do_back_screen_update 0x40cc72
    runtime_.display.copy(Surface::buffer, Surface::back, 0, 0, Display::width, Display::height);
    runtime_.setMouseShown(true);
    // load_ckey 0x405c35
    for (std::size_t i = 0; i < keyBoxes_.size(); ++i) keyBoxes_[i] = controls_.keys[i];
    mcInitDevice();
    co_await mcSlideIn();
    mcCreateForm();
    forms_.focus(0);
}

// mc_close_screen (0x406527)
Task<> MenuProgram::mcCloseScreen()
{
    co_await waitCloseKeys();
    forms_.destroy();
}

// menu_bedien (0x40654d)
Task<> MenuProgram::menuBedien()
{
    int click = SoundSystem::none;
    bool running = true;
    bool cancel = false;

    const auto bedienTmp = dataPath("BEDIEN.TMP");
    {
        const auto bytes = controls_.serialize(); // save_tmp_bedien 0x401d49
        writeFileBytes(bedienTmp, bytes);
    }
    co_await mcOpenScreen();

    while (running) {
        co_await runtime_.frame();
        if (runtime_.quitRequested) break; // [guess] window closed: leave like OK
        const int result = forms_.handle();
        switch (result) {
        case event::escape:
            forms_.focus(1);
            [[fallthrough]];
        case 1: // Annuleren
            sndPlay(click, "click1.wav");
            running = false;
            cancel = true;
            co_await sndWait(click);
            break;
        case 0: // OK
            sndPlay(click, "click1.wav");
            running = false;
            co_await sndWait(click);
            break;
        case event::up:
            sndPlay(click, "click3.wav");
            forms_.focus(forms_.focused() == 1 ? 0 : 1);
            co_await runtime_.waitKeyReleased(vk::up);
            co_await sndWait(click);
            break;
        case event::down:
            sndPlay(click, "click3.wav");
            forms_.focus(forms_.focused() == 0 ? 1 : 0);
            co_await runtime_.waitKeyReleased(vk::down);
            co_await sndWait(click);
            break;
        case 2: // Help
            sndPlay(click, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(click);
            sndSetVolume(musicHandle_, helpMusicVolume);
            if (joystickAvailable_) {
                co_await playHelp(mcHelp);
            } else {
                co_await playHelp(mcHelp2);
            }
            sndSetVolume(musicHandle_, musicVolume());
            break;
        case 3: // Toetsenbord tab
            sndPlay(click, "click2.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            if (controls_.device != keyboardForm) {
                controls_.device = keyboardForm;
                forms_.destroy();
                mcDrawKeyboardPanel();
                forms_.activate(formTables_.controlsKeyboard);
                forms_.focus(0);
            }
            co_await sndWait(click);
            break;
        case 4: // Joystick tab
        case 5: // Stuur tab
        {
            if (!joystickAvailable_) break;
            const int device = result == 4 ? joystickDevice : wheelDevice;
            sndPlay(click, "click2.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            if (controls_.device != device) {
                controls_.device = device;
                forms_.destroy();
                if (device == joystickDevice) {
                    mcDrawJoystickPanel();
                } else {
                    mcDrawWheelPanel();
                }
                forms_.activate(formTables_.controlsJoystick);
                forms_.focus(0);
            }
            co_await sndWait(click);
            break;
        }
        case 6: // Instellen
            if (controls_.device != keyboardForm) break;
            sndPlay(click, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await runtime_.waitKeyReleased(vk::escape);
            forms_.destroy();
            co_await mcInstellen();
            mcDrawKeyboardPanel();
            mcInitDevice();
            forms_.activate(formTables_.controlsKeyboard);
            forms_.focus(6);
            co_await sndWait(click);
            break;
        default:
            break;
        }
    }

    co_await mcCloseScreen();
    // save_ckey 0x405c67: the boxes become the keys.
    for (std::size_t i = 0; i < keyBoxes_.size(); ++i) controls_.keys[i] = keyBoxes_[i];
    if (cancel) {
        // load_tmp_bedien 0x401c00: a bad file is deleted and init_bedien
        // runs again.
        const auto bytes = readFileBytes(bedienTmp);
        const auto parsed = bytes ? ControlSettings::parse(*bytes) : std::nullopt;
        if (parsed) {
            controls_ = *parsed;
        } else {
            std::error_code error;
            std::filesystem::remove(bedienTmp, error);
            loadControls();
        }
    }
    std::error_code error;
    std::filesystem::remove(bedienTmp, error);
    saveControls();
}

// =============================================================================
// Opties (m_opties.cpp)
// =============================================================================

// mc_active_form (0x4068f2)
void MenuProgram::moCreateForm()
{
    forms_.destroy();
    switch (options_.displayMode) {
    case 0:
        forms_.activate(formTables_.options3d);
        break;
    case 2:
        forms_.activate(formTables_.options640);
        break;
    default:
        forms_.activate(formTables_.options320);
        break;
    }
    forms_.focus(0);
}

// mc_display_inhoud (0x406e14) with mc_display_geluid (0x406919),
// mc_display_horizon (0x406b09), mc_display_detail (0x406c0b) and
// mc_display_dashbord (0x406d12).
void MenuProgram::moDrawSettings()
{
    Display& display = runtime_.display;
    auto draw = [&display](int anim, int frame, int x, int y) {
        display.sprite(Surface::back, animSprite(anim, frame), x, y);
    };
    auto bullet = [&draw](bool selected, int x, int y) { draw(bulletAnim, selected ? 1 : 0, x, y); };

    const GameOptions& o = options_;
    auto geluid = [&] {
        draw(67, 0, 268, 245);
        draw(67, 1, 349, 245);
        draw(67, 2, 349, 266);
        draw(67, 3, 349, 287);
        draw(67, 4, 349, 308);
        bullet(o.sound == 1, 330, 246);
        bullet(o.sound == 3, 330, 267);
        bullet(o.sound == 0, 330, 288);
        bullet(o.sound == 2, 330, 309);
    };
    auto horizon = [&] {
        draw(69, 0, 257, 164);
        draw(69, 1, 349, 164);
        draw(69, 2, 349, 184);
        bullet(o.horizon == 1, 330, 165);
        bullet(o.horizon == 0, 330, 185);
    };
    auto detail = [&] {
        draw(68, 0, 476, 356);
        draw(68, 1, 552, 356);
        draw(68, 2, 552, 377);
        bullet(o.detail == 1, 533, 357);
        bullet(o.detail == 0, 533, 378);
    };
    auto dashboard = [&] {
        draw(70, 0, 233, 356);
        draw(70, 1, 349, 356);
        draw(70, 2, 349, 377);
        bullet(o.dashboard == 1, 330, 357);
        bullet(o.dashboard == 0, 330, 378);
    };

    runtime_.setMouseShown(false);
    display.copy(Surface::buffer, Surface::back, 230, 150, 600, 410);
    geluid();
    switch (o.displayMode) {
    case 0:
        dashboard();
        horizon();
        break;
    case 1:
        detail();
        break;
    case 2:
        horizon();
        detail();
        dashboard();
        break;
    default:
        break;
    }
    display.copy(Surface::back, Surface::front, 230, 150, 600, 410);
    runtime_.setMouseShown(true);
}

// mc_display_snd_setting (0x406eb0): segment i is lit when i <= volume; a
// channel switched off by GELUID shows all segments unlit.
void MenuProgram::moDrawVolume()
{
    Display& display = runtime_.display;
    const GameOptions& o = options_;
    runtime_.setMouseShown(false);
    display.sprite(Surface::back, animSprite(54, o.sound != 2 ? 1 : 0), 0, 120);
    if (o.sound != 2) {
        for (int i = 0; i < 5; ++i) {
            const int x = 46 + 26 * i;
            const int music = (i > o.musicVolume || o.sound == 1) ? 56 : 55;
            display.sprite(Surface::back, animSprite(music, i), x, 159);
            const int effects = (i > o.effectsVolume || o.sound == 3) ? 56 : 55;
            display.sprite(Surface::back, animSprite(effects, i), x, 229);
        }
    }
    display.copy(Surface::back, Surface::front, 0, 120, 203, 248);
    runtime_.setMouseShown(true);
}

// mc_display_tableau (0x406fe2): the display-mode tab bar, then everything.
void MenuProgram::moDrawTabsAndAll()
{
    int frame = 0;
    if (options_.displayMode == 1 || options_.displayMode == 2) frame = options_.displayMode;
    runtime_.setMouseShown(false);
    runtime_.display.sprite(Surface::buffer, animSprite(53, frame), 0, 112);
    runtime_.display.sprite(Surface::front, animSprite(53, frame), 0, 112);
    moDrawSettings();
    moDrawVolume();
    runtime_.setMouseShown(true);
}

// mo_animate_buttons (0x40707b)
Task<> MenuProgram::moSlideIn()
{
    runtime_.setMouseShown(false);
    for (std::size_t step = 0; step < slideOkFrames.size(); ++step) {
        const std::uint64_t start = runtime_.now();
        runtime_.display.sprite(Surface::front, animSprite(63, slideOkFrames[step]), 0, 331);
        runtime_.display.sprite(Surface::front, animSprite(64, slideCancelFrames[step]), 0, 382);
        do {
            co_await runtime_.frame();
            runtime_.discardEvents();
        } while (runtime_.now() - start < animationStepMs && !runtime_.quitRequested);
    }
    runtime_.setMouseShown(true);
}

// mo_open_screen (0x407114)
Task<> MenuProgram::moOpenScreen()
{
    loadScreen("OPTIES.PCX", true);
    runtime_.setMouseShown(false);
    co_await wipe();
    runtime_.setMouseShown(true);
    moDrawTabsAndAll();
    moDrawVolume();
    co_await moSlideIn();
    moCreateForm();
    forms_.focus(0);
}

// mo_close_screen (0x40718b)
Task<> MenuProgram::moCloseScreen()
{
    co_await waitCloseKeys();
    forms_.destroy();
}

// menu_opties (0x407242). The original receives the music buffer and the
// tune name from menu_main; the tune is mm_tune[slot.level].
Task<> MenuProgram::menuOpties()
{
    int& music = musicHandle_;
    const std::string tune(menuTune(slots_[activeSlot_].stage));
    int click = SoundSystem::none;
    bool running = true;
    bool cancel = false;

    const auto optiesTmp = dataPath("Opties.TMP");
    {
        const auto bytes = options_.serialize(); // save_tmp_optie 0x401bb8
        writeFileBytes(optiesTmp, bytes);
    }
    co_await moOpenScreen();

    GameOptions& o = options_;
    while (running) {
        co_await runtime_.frame();
        if (runtime_.quitRequested) break; // [guess] window closed: leave like OK
        const int result = forms_.handle();
        // Every option row plays click3 first; its guard is checked after the
        // click, so a rejected choice still sounds.
        if (result >= 3 && result <= 19) sndPlay(click, "click3.wav");
        switch (result) {
        case 0: // OK
            sndPlay(click, "click1.wav");
            running = false;
            co_await sndWait(click);
            break;
        case event::escape:
            forms_.focus(1);
            [[fallthrough]];
        case 1: // Annuleren
            sndPlay(click, "click1.wav");
            running = false;
            co_await sndWait(click);
            cancel = true;
            break;
        case event::up:
            sndPlay(click, "click3.wav");
            forms_.focus(forms_.focused() == 1 ? 0 : 1);
            co_await runtime_.waitKeyReleased(vk::up);
            co_await sndWait(click);
            break;
        case event::down:
            sndPlay(click, "click3.wav");
            forms_.focus(forms_.focused() == 0 ? 1 : 0);
            co_await runtime_.waitKeyReleased(vk::down);
            co_await sndWait(click);
            break;
        case 2: // Help
            sndPlay(click, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            sndSetVolume(music, helpMusicVolume);
            co_await playHelp(moHelp);
            sndSetVolume(music, musicVolume());
            co_await sndWait(click);
            break;
        case 3: // 3D Accelerator
        case 4: // 320 x 200
        case 5: // 640 x 480
        {
            const int mode = result - 3;
            // Deviation: 320 x 200 and 640 x 480 cannot be chosen; the race
            // always uses the 3D Accelerator mode. A warning says so, then
            // the Opties form comes back (the requester replaced it).
            if (mode != 0) {
                co_await reqWarning("Deze weergave is niet beschikbaar in OpenA2Racer. Het spel gebruikt altijd "
                                    "3D Accelerator.");
                moCreateForm();
                break;
            }
            if (o.displayMode == mode) break;
            if (mode == 1) {
                o.horizon = 0;
                o.dashboard = 0;
            }
            o.displayMode = static_cast<std::int16_t>(mode);
            moDrawTabsAndAll();
            moCreateForm();
            break;
        }
        case 6: // Horizon Aan
        case 7: // Horizon Uit
        {
            const std::int16_t value = result == 6 ? 1 : 0;
            if (o.horizon == value) break;
            o.horizon = value;
            moDrawSettings();
            break;
        }
        case 8:  // Effecten
        case 9:  // Muziek
        case 10: // Effecten en Muziek
        case 11: // Geen Geluid
        {
            static constexpr std::array<std::int16_t, 4> values{1, 3, 0, 2};
            const std::int16_t value = values[static_cast<std::size_t>(result - 8)];
            if (o.sound == value) break;
            o.sound = value;
            moDrawVolume();
            moDrawSettings();
            if (value == 1 || value == 2) {
                sndStop(music);
            } else {
                sndPlayLooping(music, tune);
            }
            break;
        }
        case 12: // Detail Hoog
        case 13: // Detail Laag
        {
            if (o.displayMode != 1 && o.displayMode != 2) break;
            const std::int16_t value = result == 12 ? 1 : 0;
            if (o.detail == value) break;
            o.detail = value;
            moDrawSettings();
            break;
        }
        case 14: // Dashboard Aan
        case 15: // Dashboard Uit
        {
            if (o.displayMode != 0 && o.displayMode != 2) break;
            const std::int16_t value = result == 14 ? 1 : 0;
            if (o.dashboard == value) break;
            o.dashboard = value;
            moDrawSettings();
            break;
        }
        case 16: // Muziek -
        case 17: // Muziek +
            if (o.sound != 0 && o.sound != 3) break;
            if (result == 16 && o.musicVolume > 0) --o.musicVolume;
            if (result == 17 && o.musicVolume < GameOptions::maximumVolume) ++o.musicVolume;
            sndSetVolume(music, musicVolume());
            moDrawVolume();
            break;
        case 18: // Effecten -
        case 19: // Effecten +
            if (o.sound != 0 && o.sound != 1) break;
            if (result == 18 && o.effectsVolume > 0) --o.effectsVolume;
            if (result == 19 && o.effectsVolume < GameOptions::maximumVolume) ++o.effectsVolume;
            // The click that just started is the preview of the new volume.
            sndSetVolume(click, effectVolume());
            moDrawVolume();
            break;
        default:
            break;
        }
    }

    co_await moCloseScreen();
    if (cancel) {
        const int edited = o.sound;
        // load_tmp_optie 0x401aa0: a bad file is deleted and init_option
        // runs again.
        const auto bytes = readFileBytes(optiesTmp);
        const auto parsed = bytes ? GameOptions::parse(*bytes) : std::nullopt;
        if (parsed) {
            o = *parsed;
        } else {
            std::error_code error;
            std::filesystem::remove(optiesTmp, error);
            loadOptions();
        }
        const int restored = o.sound;
        if ((restored == 0 || restored == 3) && (edited == 1 || edited == 2)) sndPlayLooping(music, tune);
        if ((restored == 1 || restored == 2) && (edited == 0 || edited == 3)) sndStop(music);
        sndSetVolume(music, musicVolume());
    }
    std::error_code error;
    std::filesystem::remove(optiesTmp, error);
    saveOptions();
}

} // namespace opena2::game
