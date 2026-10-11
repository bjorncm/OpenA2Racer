#include "menu_program.hpp"

#include "keys.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace opena2::game {
namespace {

// The menu's files in the data folder.
constexpr const char* optionsFile = "OPTIE.A2R";
constexpr const char* controlsFile = "BEDIEN.A2R";
constexpr const char* slotsFile = "SLOT.A2R";
constexpr const char* scoresFile = "SCORE.A2R";
constexpr const char* infioFile = "INFIO.INI";

// help_stop_keys, set by win_open_menu (0x401572).
constexpr std::array<int, 3> helpStopKeys{vk::enter, vk::space, vk::escape};

bool validHandle(int handle)
{
    return handle >= 0 && handle < SoundSystem::bufferCount;
}

ControlDef button(int x, int y, int anim, int hotkey, int initialState, int focusAnim = -1)
{
    ControlDef control;
    control.x = x;
    control.y = y;
    control.type = ControlType::button;
    control.anim = anim;
    control.hotkey = hotkey;
    control.focusAnim = focusAnim;
    control.initialState = initialState;
    return control;
}

// areabutton_t {w, h, anim = -1, flag = 0, hotkey}: an invisible hit rectangle.
ControlDef area(int x, int y, int width, int height, int hotkey, int initialState)
{
    ControlDef control;
    control.x = x;
    control.y = y;
    control.type = ControlType::area;
    control.width = width;
    control.height = height;
    control.hotkey = hotkey;
    control.initialState = initialState;
    return control;
}

// edit_box_t {w, h, max chars, lines = 1, text}.
ControlDef edit(int x, int y, int width, int height, int maxChars, const char* text,
                int initialState)
{
    ControlDef control;
    control.x = x;
    control.y = y;
    control.type = ControlType::edit;
    control.width = width;
    control.height = height;
    control.maxChars = maxChars;
    control.text = text;
    control.initialState = initialState;
    return control;
}

bool fileExists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void removeFile(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

void writeOrLog(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    // The original ends the program with fatal_exit when a write fails.
    if (!writeFileBytes(path, bytes)) {
        SDL_Log("OpenA2Racer: cannot write %s", path.string().c_str());
    }
}

// txt_get_word / txt_check_newline / txt_crop_word treat TAB, LF and space
// as white space.
bool isWhite(char c)
{
    return c == '\t' || c == '\n' || c == ' ';
}

// The first help stop key held with Shift up, or 0 (check_help_keys 0x41117c).
int pressedHelpKey(const Runtime& runtime)
{
    for (const int key : helpStopKeys) {
        if (runtime.keyIsPressed(key)) {
            return key;
        }
    }
    return 0;
}

// vsnprintf into the original's 256-byte buffers.
std::string formatBuffer(std::string_view text)
{
    return std::string(text.substr(0, 255));
}

// Takes the queued input off the queue and drops what could act on a
// control later: key presses, clicks and typed text. The original pumps
// messages in its waits, so such input reaches the controls at once and any
// selection it makes is lost when handle_form resets selected_control.
void discardQueuedInput(Runtime& runtime)
{
    std::vector<InputEvent> kept;
    InputEvent event;
    while (runtime.popEvent(event)) {
        if (event.type == InputEvent::Type::keyUp || event.type == InputEvent::Type::mouseUp) {
            kept.push_back(std::move(event));
        }
    }
    for (InputEvent& keptEvent : kept) {
        runtime.pushEvent(std::move(keptEvent));
    }
}

// play_help sets the focus to the main window, which does not pass keys to
// the forms, so no key pressed during the tour reaches a control. Clicks
// stay: the original delivers them to the buttons by position.
void discardQueuedKeys(Runtime& runtime)
{
    std::vector<InputEvent> kept;
    InputEvent event;
    while (runtime.popEvent(event)) {
        if (event.type == InputEvent::Type::mouseDown || event.type == InputEvent::Type::mouseUp) {
            kept.push_back(std::move(event));
        }
    }
    for (InputEvent& keptEvent : kept) {
        runtime.pushEvent(std::move(keptEvent));
    }
}

} // namespace

MenuProgram::MenuProgram(Runtime& runtime, bool first)
    : runtime_(runtime), forms_(runtime), formTables_(makeForms()), first_(first),
      soundAvailable_(runtime.sound.available())
{
}

// WinMain (0x401218) + win_handle_menu (0x4017b6).
Task<int> MenuProgram::run()
{
    if (first_) {
        // WinMain 0x4013ce: movie_play_finish (empty) and clear_screen(0, 0).
        runtime_.display.clear(Surface::front);
    }
    winOpenMenu();
    bool toMainMenu = false;
    bool started = true;
    if (first_) {
        started = co_await menuStart(true);
    } else if (co_await handleGame()) {
        toMainMenu = true;
    } else {
        started = co_await menuStart(false);
    }
    if (!toMainMenu && !started) {
        winCloseMenu();
        co_return menu_result::quit;
    }
    const int result = co_await menuMain();
    resetInfio();
    winCloseMenu();
    co_return result;
}

MenuProgram::Forms_ MenuProgram::makeForms()
{
    // Transcribed from the static form_t tables of A2MENU.DAT. The options
    // forms replace rows that a display mode lacks with 1x1 area buttons at
    // (0,0) with hot key F12 (0x433925), so the control indices stay fixed.
    Forms_ forms;
    forms.main.controls = { // 0x432e78
        button(528, 27, 18, 'H', 3),
        button(9, 27, 19, 'E', 3),
        button(0, 124, 20, 'S', 1, 32),
        button(0, 176, 21, 'B', 3, 33),
        button(0, 228, 22, 'O', 3, 34),
        button(0, 278, 24, 'C', 3, 36),
        button(0, 331, 23, 'P', 3, 35),
        button(0, 382, 25, 'N', 3, 37),
    };
    forms.start.controls = { // 0x4331b0
        button(111, 413, 6, 'S', 1),
        button(390, 413, 7, 'V', 3),
        button(528, 27, 9, 'H', 3),
        button(9, 27, 8, 'E', 3),
        area(66, 141, 505, 24, '0', 3),
        area(66, 165, 505, 24, '1', 3),
        area(66, 189, 505, 24, '2', 3),
        area(66, 213, 505, 24, '3', 3),
        area(66, 237, 505, 24, '4', 3),
        area(66, 261, 505, 24, '5', 3),
        area(66, 285, 505, 24, '6', 3),
        area(66, 309, 505, 24, '7', 3),
        area(66, 333, 505, 24, '8', 3),
        area(66, 357, 505, 24, '9', 3),
    };
    forms.newPlayer.controls = { // 0x4343dc
        button(80, 413, 1, 'S', 1),
        button(252, 413, 2, 'V', 3),
        button(425, 413, 4, 'A', 3),
        button(528, 27, 3, 'H', 3),
        area(66, 141, 505, 24, '0', 3),
        area(66, 165, 505, 24, '1', 3),
        area(66, 189, 505, 24, '2', 3),
        area(66, 213, 505, 24, '3', 3),
        area(66, 237, 505, 24, '4', 3),
        area(66, 261, 505, 24, '5', 3),
        area(66, 285, 505, 24, '6', 3),
        area(66, 309, 505, 24, '7', 3),
        area(66, 333, 505, 24, '8', 3),
        area(66, 357, 505, 24, '9', 3),
    };
    forms.controlsKeyboard.controls = { // 0x4333fc
        button(0, 331, 40, 'O', 1, 44),
        button(0, 382, 41, 'A', 3, 45),
        button(528, 27, 50, 'H', 3),
        area(313, 132, 116, 23, 'K', 0),
        area(428, 132, 74, 23, 'J', 0),
        area(500, 132, 60, 23, 'S', 0),
        button(361, 382, 49, 'I', 3),
    };
    forms.controlsJoystick.controls = { // 0x4334dc
        button(0, 331, 40, 'O', 1, 44),
        button(0, 382, 41, 'A', 3, 45),
        button(528, 27, 50, 'H', 3),
        area(313, 132, 116, 23, 'K', 4),
        area(428, 132, 74, 23, 'J', 3),
        area(500, 132, 60, 23, 'S', 3),
    };
    forms.options3d.controls = { // 0x43397c
        button(0, 331, 60, 'O', 1, 65),
        button(0, 382, 61, 'A', 3, 66),
        button(528, 27, 62, 'H', 3),
        area(226, 120, 145, 25, '1', 3),
        area(370, 120, 103, 25, '2', 3),
        area(475, 120, 105, 25, '3', 3),
        area(330, 165, 60, 22, 0x60, 0),
        area(330, 185, 60, 22, 0x61, 0),
        area(330, 246, 110, 22, 0x62, 0),
        area(330, 267, 110, 22, 0x63, 0),
        area(330, 288, 110, 22, 0x64, 0),
        area(330, 309, 110, 22, 0x65, 0),
        area(0, 0, 1, 1, 0x7B, 0),
        area(0, 0, 1, 1, 0x7B, 0),
        area(330, 357, 80, 22, 0x68, 0),
        area(330, 378, 80, 22, 0x69, 0),
        area(18, 155, 23, 22, 0x7A, 0),
        area(173, 155, 23, 22, 0x79, 0),
        area(18, 225, 23, 22, 0x78, 0),
        area(173, 225, 23, 22, 0x77, 0),
    };
    forms.options320.controls = { // 0x433bc8
        button(0, 331, 60, 'O', 1, 65),
        button(0, 382, 61, 'A', 3, 66),
        button(528, 27, 62, 'H', 3),
        area(226, 120, 145, 25, '1', 3),
        area(370, 120, 103, 25, '2', 3),
        area(475, 120, 105, 25, '3', 3),
        area(0, 0, 1, 1, 0x7B, 0),
        area(0, 0, 1, 1, 0x7B, 0),
        area(330, 246, 110, 22, 0x62, 0),
        area(330, 267, 110, 22, 0x63, 0),
        area(330, 288, 110, 22, 0x64, 0),
        area(330, 309, 110, 22, 0x65, 0),
        area(533, 357, 80, 22, 0x66, 0),
        area(533, 378, 80, 22, 0x67, 0),
        area(0, 0, 1, 1, 0x7B, 0),
        area(0, 0, 1, 1, 0x7B, 0),
        area(18, 155, 23, 22, 0x7A, 0),
        area(173, 155, 23, 22, 0x79, 0),
        area(18, 225, 23, 22, 0x78, 0),
        area(173, 225, 23, 22, 0x77, 0),
    };
    forms.options640.controls = { // 0x433e14
        button(0, 331, 60, 'O', 1, 65),
        button(0, 382, 61, 'A', 3, 66),
        button(528, 27, 62, 'H', 3),
        area(226, 120, 145, 25, '1', 3),
        area(370, 120, 103, 25, '2', 3),
        area(475, 120, 105, 25, '3', 3),
        area(330, 165, 60, 22, 0x60, 0),
        area(330, 185, 60, 22, 0x61, 0),
        area(330, 246, 110, 22, 0x62, 0),
        area(330, 267, 110, 22, 0x63, 0),
        area(330, 288, 110, 22, 0x64, 0),
        area(330, 309, 110, 22, 0x65, 0),
        area(533, 357, 80, 22, 0x66, 0),
        area(533, 378, 80, 22, 0x67, 0),
        area(330, 357, 80, 22, 0x68, 0),
        area(330, 378, 80, 22, 0x69, 0),
        area(18, 155, 23, 22, 0x7A, 0),
        area(173, 155, 23, 22, 0x79, 0),
        area(18, 225, 23, 22, 0x78, 0),
        area(173, 225, 23, 22, 0x77, 0),
    };
    forms.score.controls = { // 0x434140
        button(217, 433, 78, 'T', 1),
        area(38, 120, 76, 24, '0', 3),
        area(116, 120, 79, 24, '1', 3),
        area(197, 120, 79, 24, '2', 3),
        area(279, 120, 79, 24, '3', 3),
        area(360, 120, 79, 24, '4', 3),
        area(442, 120, 79, 24, '5', 3),
        area(522, 120, 79, 24, '6', 3),
    };
    forms.displayScore.controls = { // 0x434254
        button(217, 433, 75, 'V', 1),
    };
    forms.stand.controls = { // 0x4345b8
        button(213, 391, 76, 'V', 1),
    };
    forms.classe.controls = { // 0x434624
        button(213, 391, 77, 'V', 1),
    };
    forms.reqWarning.controls = { // 0x435898
        button(266, 307, 16, 'O', 1),
    };
    forms.reqVraag.controls = { // 0x4358fc
        button(193, 307, 13, 'J', 1),
        button(345, 307, 14, 'N', 3),
    };
    forms.reqInput.controls = { // 0x4359a8
        button(193, 307, 10, 'O', 1),
        button(345, 307, 11, 'A', 3),
        edit(176, 237, 200, 27, 23, "Speler", 1),
    };

    return forms;
}

// win_open_menu (0x4014c8).
void MenuProgram::winOpenMenu()
{
    runtime_.setMouseEnabled(true);
    runtime_.display.clear(Surface::front);
    runtime_.display.clear(Surface::buffer);
    runtime_.display.clear(Surface::back);
    std::error_code error;
    std::filesystem::create_directories(runtime_.dataFolder, error);
    loadOptions();
    loadControls();
    loadSlots();
    loadScores();
    loadInfio();
    // The help stop keys (Enter, Space, Esc) are the constant helpStopKeys;
    // the five Arial Black fonts belong to Display.
}

// win_close_menu (0x4016c9).
void MenuProgram::winCloseMenu()
{
    runtime_.setMouseEnabled(false);
    runtime_.display.clear(Surface::front);
    runtime_.display.clear(Surface::buffer);
    runtime_.display.clear(Surface::back);
    saveOptions();
    saveControls();
    saveSlots();
    saveScores();
    saveInfio();
}

// init_optie (0x401afe) and load_tmp_optie (0x401aa0): a missing file gets
// the defaults; a short read deletes the file and starts over.
void MenuProgram::loadOptions()
{
    const auto path = dataPath(optionsFile);
    if (!fileExists(path)) {
        options_ = GameOptions{};
        // Deviation: only the 3D Accelerator mode is offered (the original
        // defaults to 320 x 200), and the dashboard is on (the original
        // defaults to off).
        options_.displayMode = 0;
        options_.dashboard = 1;
        saveOptions();
        return;
    }
    const auto bytes = readFileBytes(path);
    const auto parsed = bytes ? GameOptions::parse(*bytes) : std::nullopt;
    if (!parsed) {
        removeFile(path);
        loadOptions();
        return;
    }
    options_ = *parsed;
    options_.displayMode = 0;
}

// save_optie (0x401bb3).
void MenuProgram::saveOptions()
{
    const auto bytes = options_.serialize();
    writeOrLog(dataPath(optionsFile), bytes);
}

// init_bedien (0x401c5e) and load_tmp_bedien (0x401c00).
void MenuProgram::loadControls()
{
    // get_attached_joysticks (0x413b10) > 0.
    static const bool joystickSubsystem = SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    int count = 0;
    if (SDL_JoystickID* joysticks = SDL_GetJoysticks(&count)) {
        SDL_free(joysticks);
    }
    joystickAvailable_ = joystickSubsystem && count > 0;

    const auto path = dataPath(controlsFile);
    if (!fileExists(path)) {
        controls_ = ControlSettings{};
        saveControls();
        return;
    }
    const auto bytes = readFileBytes(path);
    const auto parsed = bytes ? ControlSettings::parse(*bytes) : std::nullopt;
    if (!parsed) {
        removeFile(path);
        loadControls();
        return;
    }
    controls_ = *parsed;
}

// save_bedien (0x401d30).
void MenuProgram::saveControls()
{
    const auto bytes = controls_.serialize();
    writeOrLog(dataPath(controlsFile), bytes);
}

// init_slotdbase (0x4029de): a missing SLOT.A2R becomes ten empty slots
// holding the current options and controls.
void MenuProgram::loadSlots()
{
    if (!fileExists(dataPath(slotsFile))) {
        slots_ = SlotDatabase::empty(options_, controls_);
        saveSlots();
        return;
    }
    loadSlotsFrom(slotsFile);
}

// save_slotdbase (0x402b34).
void MenuProgram::saveSlots()
{
    saveSlotsTo(slotsFile);
}

// save_tmp_slotdbase.
void MenuProgram::saveSlotsTo(const std::string& file)
{
    writeOrLog(dataPath(file), slots_.serialize());
}

// load_tmp_slotdbase (0x402979): a short read deletes `file` and re-runs
// init_slotdbase, which reads SLOT.A2R.
void MenuProgram::loadSlotsFrom(const std::string& file)
{
    const auto path = dataPath(file);
    if (!fileExists(path)) {
        // fatal_exit in the original. [guess] keep the slots in memory.
        SDL_Log("OpenA2Racer: %s is missing", path.string().c_str());
        return;
    }
    const auto bytes = readFileBytes(path);
    const auto parsed = bytes ? SlotDatabase::parse(*bytes) : std::nullopt;
    if (!parsed) {
        removeFile(path);
        loadSlots();
        return;
    }
    slots_ = *parsed;
}

// init_scoredbase (0x4033b9): a missing SCORE.A2R is written from the
// built-in lists; the new-entry flags are cleared either way.
void MenuProgram::loadScores()
{
    if (!fileExists(dataPath(scoresFile))) {
        scores_ = ScoreDatabase::defaults();
        saveScores();
    } else {
        loadScoresFrom(scoresFile);
    }
    scores_.clearNewFlags();
}

// save_scoredbase (0x40346b).
void MenuProgram::saveScores()
{
    saveScoresTo(scoresFile);
}

void MenuProgram::saveScoresTo(const std::string& file)
{
    writeOrLog(dataPath(file), scores_.serialize());
}

// load_tmp_scoredbase (0x403354).
void MenuProgram::loadScoresFrom(const std::string& file)
{
    const auto path = dataPath(file);
    if (!fileExists(path)) {
        // fatal_exit in the original. [guess] keep the scores in memory.
        SDL_Log("OpenA2Racer: %s is missing", path.string().c_str());
        return;
    }
    const auto bytes = readFileBytes(path);
    const auto parsed = bytes ? ScoreDatabase::parse(*bytes) : std::nullopt;
    if (!parsed) {
        // The original re-saves whatever the failed read left in memory;
        // the built-in lists are what a fresh install has there.
        removeFile(path);
        loadScores();
        return;
    }
    scores_ = *parsed;
}

// init_infio (0x403f9b) and load_infio (0x4038f4).
void MenuProgram::loadInfio()
{
    const auto path = dataPath(infioFile);
    if (!fileExists(path)) {
        infio_ = Infio{};
        saveInfio();
        return;
    }
    const auto bytes = readFileBytes(path);
    if (!bytes) {
        removeFile(path);
        loadInfio();
        return;
    }
    infio_ = Infio::parse(std::string(bytes->begin(), bytes->end()));
}

// save_infio (0x403c46).
void MenuProgram::saveInfio()
{
    const std::string text = infio_.serialize();
    writeOrLog(dataPath(infioFile),
               std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

// reset_infio (0x40ad6c): fills INFIO from the active slot, the options and
// the controls before a race.
void MenuProgram::resetInfio()
{
    const PlayerSlot& slot = slots_[activeSlot_];
    infio_.raceFinished = 0;
    infio_.money = slot.money;
    infio_.activeSlot = static_cast<std::int16_t>(activeSlot_);
    infio_.sound = options_.sound;
    infio_.resolution = options_.resolution;
    infio_.view = options_.view;
    infio_.control = static_cast<std::int16_t>(controls_.device);
    infio_.displayMode = options_.displayMode;
    infio_.horizon = options_.horizon;
    infio_.dashboard = options_.dashboard;
    infio_.detail = options_.detail;
    infio_.camera = options_.camera;
    infio_.musicVolume = options_.musicVolume;
    infio_.effectsVolume = options_.effectsVolume;
    infio_.stage = slot.stage;
    // The starting grid is the result of the previous stage.
    const int previous = std::min<int>(slot.stage, stageCount) - 1;
    const bool started = slot.stage >= 1;
    infio_.position = started ? slot.playerPosition[static_cast<std::size_t>(previous)]
                              : std::int16_t{6};
    for (std::size_t racer = 0; racer < infio_.racerPosition.size(); ++racer) {
        infio_.racerPosition[racer] =
            started ? slot.racerPosition[racer][static_cast<std::size_t>(previous)]
                    : static_cast<std::int16_t>(racer + 1);
    }
    infio_.time = 0;
    infio_.racerTime.fill(0);
    infio_.finale = 0;
    infio_.finishedBy = 0;
    for (std::size_t i = 0; i < infio_.keys.size(); ++i) {
        infio_.keys[i] = static_cast<std::int16_t>(controls_.keys[i]);
    }
}

// set_active_slot (0x402c46). The original stops with fatal_exit on an
// unused slot.
void MenuProgram::setActiveSlot(std::size_t slot)
{
    options_ = slots_[slot].options;
    controls_ = slots_[slot].controls;
    activeSlot_ = slot;
}

// update_active_slot_optie (0x402d21).
void MenuProgram::updateActiveSlotOptions()
{
    if (slots_[activeSlot_].used) {
        slots_[activeSlot_].options = options_;
    }
}

// update_active_slot_bediening (0x402dce).
void MenuProgram::updateActiveSlotControls()
{
    if (slots_[activeSlot_].used) {
        slots_[activeSlot_].controls = controls_;
    }
}

std::filesystem::path MenuProgram::dataPath(const std::string& file) const
{
    return runtime_.dataFolder / file;
}

// snd_play (0x40b9a9): nothing without a sound device or with GELUID 2
// ("Geen Geluid"); a new sound on a handle stops the previous one.
void MenuProgram::sndPlay(int& handle, std::string_view wav)
{
    if (!soundAvailable_) {
        handle = SoundSystem::none;
        return;
    }
    if (options_.sound == 2) {
        return;
    }
    sndStop(handle);
    handle = playSoundBuffer(wav);
    sndSetVolume(handle, effectVolume());
}

// snd_play_looping (0x40ba14): as snd_play, looped, at the music volume.
void MenuProgram::sndPlayLooping(int& handle, std::string_view wav)
{
    if (!soundAvailable_) {
        handle = SoundSystem::none;
        return;
    }
    if (options_.sound == 2) {
        return;
    }
    sndStop(handle);
    const PcmWave* wave = runtime_.assets.sound(wav);
    handle = wave != nullptr ? runtime_.sound.play(*wave, true) : SoundSystem::none;
    sndSetVolume(handle, musicVolume());
}

// snd_stop (0x40b945).
void MenuProgram::sndStop(int& handle)
{
    if (validHandle(handle) && runtime_.sound.busy(handle)) {
        runtime_.sound.stop(handle);
    }
    handle = SoundSystem::none;
}

// snd_wait (0x40ba82): blocks, pumping only messages, until the sound ends.
Task<> MenuProgram::sndWait(int& handle)
{
    if (validHandle(handle) && options_.sound != 2) {
        while (runtime_.sound.busy(handle) && !runtime_.quitRequested) {
            co_await runtime_.frame();
            discardQueuedInput(runtime_);
        }
    }
    sndStop(handle);
}

// snd_set_volume (0x40bb4f). With GELUID 2 the sound is stopped and the
// caller's handle cleared instead.
void MenuProgram::sndSetVolume(int& handle, int hundredthsOfDecibel)
{
    if (!validHandle(handle)) {
        return;
    }
    if (options_.sound != 2) {
        runtime_.sound.setVolume(handle, hundredthsOfDecibel);
        return;
    }
    if (runtime_.sound.busy(handle)) {
        runtime_.sound.stop(handle);
    }
    handle = SoundSystem::none;
}

// play_sound_buffer (0x40ecd3): full volume, returns the buffer handle.
int MenuProgram::playSoundBuffer(std::string_view wav)
{
    const PcmWave* wave = runtime_.assets.sound(wav);
    if (wave == nullptr || !runtime_.sound.available()) {
        return SoundSystem::none;
    }
    return runtime_.sound.play(*wave, false);
}

int MenuProgram::effectVolume() const
{
    return -100 - 500 * (GameOptions::maximumVolume - options_.effectsVolume);
}

int MenuProgram::musicVolume() const
{
    return -100 - 500 * (GameOptions::maximumVolume - options_.musicVolume);
}

int MenuProgram::duckedMusicVolume() const
{
    return -100 - 500 * (GameOptions::maximumVolume - options_.musicVolume / 2);
}

// load_pcx_file(2, name, setPalette ? 0x1f : 0x19).
void MenuProgram::loadScreen(const std::string& pcx, bool setPalette)
{
    runtime_.display.loadImage(Surface::buffer, upperCase(pcx), setPalette);
}

// handle_wipe(2, 3, 600) -> wipe_pixelfade_full (0x41e048). The original
// copies the four raster steps with no delay at all; one frame per step
// keeps the dissolve visible. [guess]
Task<> MenuProgram::wipe()
{
    runtime_.setMouseShown(false);
    for (int step = 0; step < 4; ++step) {
        runtime_.display.wipeStep(Surface::buffer, step);
        co_await runtime_.frame();
    }
    runtime_.setMouseShown(true);
}

// play_help (0x41105f) with the back surface as scratch: each control is
// lit on a darkened screen while its voice plays; a stop key ends the tour.
Task<> MenuProgram::playHelp(std::span<const HelpEntry> entries)
{
    Display& display = runtime_.display;
    runtime_.setMouseEnabled(false);
    const int savedFocus = forms_.focused();
    forms_.focus(-1);
    display.copy(Surface::front, Surface::back, 0, 0, Display::width, Display::height);
    int handle = SoundSystem::none;
    bool abort = false;
    for (const HelpEntry& entry : entries) {
        if (entry.control == -1 || abort) {
            break;
        }
        display.grid(Surface::front);
        handle = playSoundBuffer(entry.wav != nullptr ? entry.wav : "");
        forms_.drawHelp(entry.control);
        while (validHandle(handle) && runtime_.sound.busy(handle)) {
            if (const int key = pressedHelpKey(runtime_); key != 0) {
                // check_help_keys waits until the key is released.
                while (runtime_.keyIsPressed(key) && !runtime_.quitRequested) {
                    co_await runtime_.frame();
                }
                abort = true;
                break;
            }
            if (runtime_.quitRequested) {
                abort = true;
                break;
            }
            co_await runtime_.frame();
        }
    }
    if (validHandle(handle)) {
        runtime_.sound.stop(handle);
    }
    display.copy(Surface::back, Surface::front, 0, 0, Display::width, Display::height);
    discardQueuedKeys(runtime_);
    if (savedFocus >= 0) {
        forms_.focus(savedFocus);
    }
    runtime_.setMouseEnabled(true);
}

// check_help_keys (0x41117c) without its wait for the release, which needs
// frames; playHelp does that wait.
bool MenuProgram::checkHelpKeys()
{
    return pressedHelpKey(runtime_) != 0;
}

// The wait at the start of every *_close_screen: Esc, the last form hot
// key and Enter released.
Task<> MenuProgram::waitCloseKeys()
{
    co_await runtime_.waitKeyReleased(vk::escape);
    co_await forms_.waitHotkeyReleased();
    co_await runtime_.waitKeyReleased(vk::enter);
    discardQueuedInput(runtime_);
}

// win_print_xy (0x412443): (x, y) is the top-left of the text cell.
void MenuProgram::print(Surface surface, int font, Rgb color, int x, int y, std::string_view text)
{
    runtime_.display.text(surface, font, color, x, y, text);
}

// win_bprintf (0x40ac58): trims trailing white space, then replaces the
// end by "..." until the text is narrower than maxWidth.
void MenuProgram::printEllipsis(Surface surface, int font, Rgb color, int x, int y,
                                int maxWidth, std::string_view text)
{
    std::string line = formatBuffer(text);
    for (int i = static_cast<int>(line.size()) - 1;
         i > 0 && (line[i] == ' ' || line[i] == '\0' || line[i] == '\n' || line[i] == '\t'); --i) {
        line.resize(static_cast<std::size_t>(i));
    }
    const Display& display = runtime_.display;
    for (;;) {
        const int width = display.textWidth(font, line);
        // The original indexes before the buffer for texts shorter than 4.
        if (width < maxWidth || width <= 4 || line.size() < 4) {
            break;
        }
        const std::size_t length = line.size();
        line.resize(length - 1);
        line[length - 2] = '.';
        line[length - 3] = '.';
        line[length - 4] = '.';
    }
    print(surface, font, color, x, y, line);
}

// win_printwrapped (0x40aac3): word by word (leading white space plus the
// following run), breaking before a word that reaches x + maxWidth or whose
// leading white space holds a line feed.
void MenuProgram::printWrapped(Surface surface, int font, Rgb color, int x, int y, int maxWidth,
                               std::string_view text)
{
    const std::string message = formatBuffer(text);
    const Display& display = runtime_.display;
    const int right = x + maxWidth;
    int cx = x;
    int cy = y;
    std::size_t i = 0;
    while (i < message.size()) {
        const std::size_t start = i;
        while (i < message.size() && isWhite(message[i])) {
            ++i;
        }
        const std::size_t wordStart = i;
        while (i < message.size() && !isWhite(message[i])) {
            ++i;
        }
        const std::string_view leading(message.data() + start, wordStart - start);
        std::string word = message.substr(start, i - start);
        int width = display.textWidth(font, word);
        if (cx + width >= right || leading.find('\n') != std::string_view::npos) {
            // GetTextExtentPoint32("H").cy of the current font.
            cy += display.textHeight(font);
            word = message.substr(wordStart, i - wordStart);
            cx = x;
            width = display.textWidth(font, word);
        }
        print(surface, font, color, cx, cy, word);
        cx += width;
    }
}

} // namespace opena2::game
