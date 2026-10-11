// m_main.cpp of A2MENU.DAT: the main menu (HOOFD.PCX).

#include "menu_program.hpp"

#include "anims.hpp"
#include "keys.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace opena2::game {
namespace {

// mm_form control indices.
namespace control {
constexpr int help = 0;
constexpr int einde = 1;
constexpr int start = 2;
constexpr int bediening = 3;
constexpr int opties = 4;
constexpr int score = 5;
constexpr int opnieuw = 6;
constexpr int nieuweSpeler = 7;
} // namespace control

// mm_help (0x432f74).
constexpr std::array<HelpEntry, 7> mainHelp{{
    {control::start, "MM_00.wav"},
    {control::bediening, "MM_01.wav"},
    {control::opties, "MM_02.wav"},
    {control::score, "MM_05.wav"},
    {control::opnieuw, "MM_04.wav"},
    {control::nieuweSpeler, "MM_03.wav"},
    {control::einde, "MNS_05.wav"},
}};

// mm_tune (0x432fb4): menu music by the stage about to be driven.
constexpr std::array<const char*, stageCount> mainTunes{
    "speed.wav", "energy.wav", "jammin.wav", "moody.wav", "disco.wav", "speed2.wav",
};

// Slide-in of mm_animate_buttons (0x404400): the anim and y of each big
// button, and the frame per step (mm_entree_anim_tabel 0x432fd0).
struct SlideRow {
    int anim;
    int y;
    std::array<int, 12> frames;
};

constexpr std::array<SlideRow, 6> slideRows{{
    {26, 124, {1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7}}, // Start
    {27, 176, {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7}}, // Bediening
    {28, 228, {0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7}}, // Opties
    {30, 278, {0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 7}}, // Score
    {29, 331, {0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7}}, // Opnieuw
    {31, 382, {0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7}}, // Nieuwe speler
}};
constexpr int slideStepMilliseconds = 33;

// Info panel rectangle on HOOFD.PCX.
constexpr int infoLeft = 500;
constexpr int infoTop = 135;
constexpr int infoRight = 610;
constexpr int infoBottom = 235;

// Route sign (anim 38, one frame per stage).
constexpr int mapAnim = 38;
constexpr int mapX = 349;
constexpr int mapY = 245;

constexpr int volumeMuted = -8000;

constexpr const char* opnieuwQuestion =
    "De huidige race wordt afgelast. Alle coureurs worden geacht opnieuw te beginnen.\n"
    "Weet je zeker dat je opnieuw wil beginnen?";
constexpr const char* nieuweSpelerQuestion = "Weet je zeker dat je de huidige race wil onderbreken?";

int clampStage(int stage)
{
    return std::clamp(stage, 0, stageCount - 1);
}

// Up/Down focus cycling over the six big buttons (jump tables 0x404a44 and
// 0x404a5c).
int focusAbove(int focused)
{
    switch (focused) {
    case control::bediening: return control::start;
    case control::opties: return control::bediening;
    case control::score: return control::opties;
    case control::opnieuw: return control::score;
    case control::nieuweSpeler: return control::opnieuw;
    default: return control::nieuweSpeler;
    }
}

int focusBelow(int focused)
{
    switch (focused) {
    case control::start: return control::bediening;
    case control::bediening: return control::opties;
    case control::opties: return control::score;
    case control::score: return control::opnieuw;
    case control::opnieuw: return control::nieuweSpeler;
    default: return control::start;
    }
}

std::string formatNumber(const char* format, int value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, value);
    return buffer;
}

std::string formatText(const char* format, const std::string& value)
{
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, format, value.c_str());
    return buffer;
}

} // namespace

// mm_draw_info_text (0x404196): the five values of the info panel, on the
// back surface, offset by (dx, dy) from x=517.
void MenuProgram::mmDrawInfoText(int dx, int dy, Rgb color)
{
    const int x = 517 + dx;
    const auto& slot = slots_[activeSlot_];

    printEllipsis(Surface::back, font::info, color, x, 140 + dy, 90,
                  formatText("%-16s", slots_.nameText(activeSlot_)));
    print(Surface::back, font::info, color, x, 157 + dy, formatNumber("%-9d", slot.money));
    print(Surface::back, font::info, color, x, 174 + dy,
          formatText("%-6s", slots_.timeText(activeSlot_)));

    const char* device = "Onbekend";
    switch (controls_.device) {
    case 0: device = "Toetsenbord"; break;
    case 1: device = "Joypad 1"; break;
    case 2: device = "Stuur"; break;
    default: break;
    }
    printEllipsis(Surface::back, font::info, color, x, 191 + dy, 90, device);

    const char* display = "Onbekend";
    switch (options_.displayMode) {
    case 0: display = "3D Accelerator"; break;
    case 1: display = "320 x 200"; break;
    case 2: display = "640 x 480"; break;
    default: break;
    }
    printEllipsis(Surface::back, font::info, color, x, 209 + dy, 90, display);
}

// mm_display_info (0x4042ea): two black passes at +2 and +1, then the text
// in RGB(245,245,245), composed on the back surface over a clean copy of
// the background.
void MenuProgram::mmDisplayInfo()
{
    auto& display = runtime_.display;
    display.copy(Surface::buffer, Surface::back, infoLeft, infoTop, infoRight, infoBottom);
    mmDrawInfoText(0, 0, colorRef(0x000000));
    mmDrawInfoText(-1, -1, colorRef(0x000000));
    mmDrawInfoText(-2, -2, colorRef(0xF5F5F5));
    runtime_.setMouseShown(false);
    display.copy(Surface::back, Surface::front, infoLeft, infoTop, infoRight, infoBottom);
    runtime_.setMouseShown(true);
}

// mm_animate_buttons (0x404400): 12 steps of 33 ms, drawn straight onto the
// front surface.
Task<> MenuProgram::mmAnimateButtons()
{
    runtime_.setMouseShown(false);
    for (std::size_t step = 0; step < 12; ++step) {
        const auto start = runtime_.now();
        for (const auto& row : slideRows) {
            runtime_.display.sprite(Surface::front, animSprite(row.anim, row.frames[step]), 0, row.y);
        }
        while (runtime_.now() - start < slideStepMilliseconds && !runtime_.quitRequested) {
            co_await runtime_.frame();
        }
    }
    runtime_.setMouseShown(true);
}

// mm_animate_map (0x404564): the route sign of the stage about to be driven.
void MenuProgram::mmAnimateMap(int level)
{
    runtime_.setMouseShown(false);
    // The original exits with a fatal error outside 0..5. [guess: clamp]
    const int sprite = animSprite(mapAnim, clampStage(level));
    auto& display = runtime_.display;
    display.sprite(Surface::back, sprite, mapX, mapY);
    display.copy(Surface::back, Surface::front, mapX, mapY, mapX + display.spriteWidth(sprite),
                 mapY + display.spriteHeight(sprite));
    runtime_.setMouseShown(true);
}

// mm_open_screen (0x404610): only the first open sets HOOFD.PCX's palette.
Task<> MenuProgram::mmOpenScreen(bool first)
{
    loadScreen("HOOFD.PCX", first);
    runtime_.setMouseShown(false);
    co_await wipe();
    runtime_.setMouseShown(true);
    mmDisplayInfo();
    co_await mmAnimateButtons();
    mmAnimateMap(slots_[activeSlot_].stage);
    // No form has focus until activate_form, so the original's message pump
    // hands every key and click made so far to the main window, which
    // ignores them.
    runtime_.discardEvents();
    forms_.activate(formTables_.main);
}

// mm_close_screen (0x404724): sub-screens wipe straight over the main menu;
// leaving the menu fades to black first.
Task<> MenuProgram::mmCloseScreen(bool toBlack)
{
    co_await waitCloseKeys();
    forms_.destroy();
    if (toBlack) {
        runtime_.display.clear(Surface::buffer, 0);
        co_await wipe();
    }
}

// mm_handle_tmp_hiscore (0x404798): the Score button shows the hiscore list
// with the player's running total, then restores the list from SCORE.TMP.
Task<> MenuProgram::mmHandleTmpHiscore()
{
    saveScoresTo("SCORE.TMP");
    const auto& slot = slots_[activeSlot_];
    const int level = slot.stage;
    if (level - 1 >= 0) {
        scores_.addHiscore(slot.name, slot.playerTotal(level), level - 1, slot.money);
    }
    co_await menuScore(level - 1);
    loadScoresFrom("SCORE.TMP");
    std::error_code error;
    std::filesystem::remove(dataPath("SCORE.TMP"), error);
}

// mm_handle_opnieuw_spel (0x4048df): restarting a career that has driven a
// stage enters its total in the hiscore list for good.
Task<> MenuProgram::mmHandleOpnieuwSpel()
{
    const int level = slots_[activeSlot_].stage;
    if (level - 1 < 0) {
        slots_.reset(activeSlot_, options_, controls_);
        mmDisplayInfo();
        co_return;
    }
    co_await mmCloseScreen(false);
    const auto& slot = slots_[activeSlot_];
    const int place = scores_.addHiscore(slot.name, slot.playerTotal(level), level - 1, slot.money);
    if (place >= 0 && place < 10) {
        co_await menuScore(level - 1);
    }
    slots_.reset(activeSlot_, options_, controls_);
    co_await mmOpenScreen(false);
}

// menu_main (0x404a74): returns menu_result::startRace or menu_result::quit.
Task<int> MenuProgram::menuMain()
{
    bool run = true;
    int result = menu_result::quit;
    int sound = SoundSystem::none;

    // The tune starts once and plays on through every sub-screen.
    if (options_.sound == 0 || options_.sound == 3) {
        sndPlayLooping(musicHandle_, mainTunes[static_cast<std::size_t>(clampStage(slots_[activeSlot_].stage))]);
    }
    co_await mmOpenScreen(true);

    while (run) {
        if (runtime_.quitRequested) {
            // Window closed: leave like Einde. [guess: the original ignores WM_QUIT here]
            break;
        }
        const int event = forms_.handle();
        switch (event) {
        case control::help:
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(sound);
            sndSetVolume(musicHandle_, volumeMuted);
            co_await playHelp(mainHelp);
            sndSetVolume(musicHandle_, musicVolume());
            break;
        case event::escape:
            forms_.focus(control::einde);
            [[fallthrough]];
        case control::einde:
            sndPlay(sound, "click1.wav");
            run = false;
            co_await sndWait(sound);
            break;
        case control::start:
            result = menu_result::startRace;
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            run = false;
            co_await sndWait(sound);
            resetInfio();
            break;
        case control::bediening:
            sndPlay(sound, "click1.wav");
            co_await mmCloseScreen(false);
            co_await sndWait(sound);
            co_await menuBedien();
            updateActiveSlotControls();
            co_await mmOpenScreen(false);
            break;
        case control::opties:
            sndPlay(sound, "click1.wav");
            co_await mmCloseScreen(false);
            co_await sndWait(sound);
            co_await menuOpties();
            updateActiveSlotOptions();
            co_await mmOpenScreen(false);
            break;
        case control::score:
            sndPlay(sound, "click1.wav");
            co_await mmCloseScreen(false);
            co_await sndWait(sound);
            co_await mmHandleTmpHiscore();
            co_await mmOpenScreen(false);
            break;
        case control::opnieuw:
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(sound);
            sndSetVolume(musicHandle_, duckedMusicVolume());
            sndPlay(sound, "MM_07.wav");
            if (co_await reqVraag(opnieuwQuestion)) {
                sndStop(sound);
                co_await mmHandleOpnieuwSpel();
            }
            sndSetVolume(musicHandle_, musicVolume());
            sndStop(sound);
            forms_.activate(formTables_.main);
            break;
        case control::nieuweSpeler:
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(sound);
            sndSetVolume(musicHandle_, duckedMusicVolume());
            sndPlay(sound, "MM_06.wav");
            if (!co_await reqVraag(nieuweSpelerQuestion)) {
                sndStop(sound);
                forms_.activate(formTables_.main);
                sndSetVolume(musicHandle_, musicVolume());
                break;
            }
            sndStop(sound);
            sndSetVolume(musicHandle_, musicVolume());
            co_await mmCloseScreen(false);
            co_await menuNieuweSpeler();
            co_await mmOpenScreen(false);
            break;
        case event::up:
            sndPlay(sound, "click3.wav");
            forms_.focus(focusAbove(forms_.focused()));
            co_await runtime_.waitKeyReleased(vk::up);
            co_await sndWait(sound);
            break;
        case event::down:
            sndPlay(sound, "click3.wav");
            forms_.focus(focusBelow(forms_.focused()));
            co_await runtime_.waitKeyReleased(vk::down);
            co_await sndWait(sound);
            break;
        default:
            break;
        }
        co_await runtime_.frame();
    }

    co_await sndWait(sound);
    co_await mmCloseScreen(true);
    sndStop(musicHandle_);
    co_return result;
}

} // namespace opena2::game
