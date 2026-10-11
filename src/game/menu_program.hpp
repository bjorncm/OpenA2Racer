#pragma once

#include "forms.hpp"
#include "runtime.hpp"
#include "task.hpp"

#include "opena2/menu_data.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace opena2::game {

// Exit codes of the menu program, which A2.EXE acts on.
namespace menu_result {
constexpr int startRace = 0x28;
constexpr int quit = 0xFF;
} // namespace menu_result

// Fonts created by win_open_menu (0x4014c8): Arial Black with GDI cell
// heights 13, 20, 23, 18, 18.
namespace font {
constexpr int small = 0;
constexpr int requester = 1;
constexpr int list = 2;
constexpr int controls = 3;
constexpr int info = 4;
} // namespace font

// Port of A2MENU.DAT, the menu program A2.EXE starts between races. Each
// member function carries the name and address of the original routine it
// ports.
//
// Implementation files:
//   menu_program.cpp  win_handle_menu, open/close, files, INFIO, sound, help
//   menu_main.cpp     m_main.cpp (main menu)
//   menu_players.cpp  m_start.cpp, m_nsplr.cpp (player lists)
//   menu_requesters.cpp reqwarn/reqvraag/reqinput, text helpers
//   menu_settings.cpp m_opties.cpp, m_bedien.cpp
//   menu_scores.cpp   m_score.cpp, m_dscore.cpp, m_stand.cpp, m_clas.cpp,
//                     handle_game
class MenuProgram {
public:
    MenuProgram(Runtime& runtime, bool first);

    // WinMain + win_handle_menu (0x4017b6): returns menu_result::startRace or
    // menu_result::quit.
    Task<int> run();

private:
    // --- menu_program.cpp ----------------------------------------------------
    void winOpenMenu();                        // 0x4014c8 (loads all files)
    void winCloseMenu();                       // 0x4016c9 (saves all files)
    void loadOptions();                        // init_option 0x401afe
    void saveOptions();
    void loadControls();                       // init_bedien 0x401c5e
    void saveControls();
    void loadSlots();                          // init_slotdbase 0x4029de
    void saveSlots();                          // save_slotdbase 0x402b34
    void saveSlotsTo(const std::string& file); // save_tmp_slotdbase
    void loadSlotsFrom(const std::string& file);
    void loadScores();                         // init_scoredbase 0x4033b9
    void saveScores();
    void saveScoresTo(const std::string& file);
    void loadScoresFrom(const std::string& file);
    void loadInfio();                          // init_infio 0x403f9b
    void saveInfio();                          // save_infio 0x403c46
    void resetInfio();                         // reset_infio 0x40ad6c
    void setActiveSlot(std::size_t slot);      // set_active_slot 0x402c46
    void updateActiveSlotOptions();            // 0x402d21
    void updateActiveSlotControls();           // 0x402dce
    std::filesystem::path dataPath(const std::string& file) const;

    // Sound wrappers (mysound.cpp 0x40b945..0x40bb4f).
    void sndPlay(int& handle, std::string_view wav);        // snd_play
    void sndPlayLooping(int& handle, std::string_view wav); // snd_play_looping
    void sndStop(int& handle);                              // snd_stop
    Task<> sndWait(int& handle);                            // snd_wait
    void sndSetVolume(int& handle, int hundredthsOfDecibel); // snd_set_volume
    int playSoundBuffer(std::string_view wav);              // play_sound_buffer
    int effectVolume() const;   // -100 - 500 * (4 - VEFFECT)
    int musicVolume() const;    // -100 - 500 * (4 - VMUZIEK)
    int duckedMusicVolume() const; // -100 - 500 * (4 - VMUZIEK / 2)

    // Screen helpers.
    void loadScreen(const std::string& pcx, bool setPalette); // load_pcx_file(2, ...)
    Task<> wipe();              // handle_wipe(2, 3, 600): pixel-fade buffer -> screen
    Task<> playHelp(std::span<const HelpEntry> entries); // play_help 0x41105f
    bool checkHelpKeys();       // check_help_keys 0x41117c (Enter, Space, Esc)
    Task<> waitCloseKeys();     // Esc, form hotkey and Enter released

    // Text (font.cpp, txt helpers).
    void print(Surface surface, int font, Rgb color, int x, int y, std::string_view text);
    void printEllipsis(Surface surface, int font, Rgb color, int x, int y, int maxWidth,
                       std::string_view text);          // win_bprintf 0x40ac58
    void printWrapped(Surface surface, int font, Rgb color, int x, int y, int maxWidth,
                      std::string_view text);           // win_printwrapped 0x40aac3

    // --- menu_main.cpp (m_main.cpp) ----------------------------------------
    Task<int> menuMain();                       // 0x404a74
    Task<> mmOpenScreen(bool first);            // 0x404610
    Task<> mmCloseScreen(bool toBlack);         // 0x404724
    void mmDisplayInfo();                       // 0x4042ea
    void mmDrawInfoText(int dx, int dy, Rgb color); // 0x404196
    Task<> mmAnimateButtons();                  // 0x404400
    void mmAnimateMap(int level);               // 0x404564
    Task<> mmHandleTmpHiscore();                // 0x404798
    Task<> mmHandleOpnieuwSpel();               // 0x4048df

    // --- menu_players.cpp (m_start.cpp, m_nsplr.cpp) ------------------------
    Task<bool> menuStart(bool first);           // 0x4055ed
    Task<> mstHandleIntro(bool show);           // 0x405429
    Task<> mstOpenScreen(std::size_t selected); // 0x4054d9
    Task<> mstCloseScreen();                    // 0x405598
    Task<bool> menuNieuweSpeler();              // 0x408970
    Task<> mnsOpenScreen(std::size_t selected); // 0x4088b8
    Task<> mnsCloseScreen();                    // 0x408940
    void drawSlotRow(std::size_t slot, bool selected); // mst/mns_draw_(selected_)slot_info
    void printSlots(std::size_t selected);      // mst/mns_print_slot
    void reselectSlot(std::size_t oldSlot, std::size_t newSlot); // mst/mns_reselect_slot

    // --- menu_requesters.cpp (reqwarn, reqvraag, reqinput) ------------------
    Task<> reqWarning(std::string message);     // req_warning 0x40a308
    Task<bool> reqVraag(std::string message);   // req_vraag 0x40a571
    Task<bool> reqInput(std::string& result, std::string message); // req_input 0x40a85e
    void reqOpen(int windowAnim, std::string_view message);          // shared open sequence
    Task<> reqClose();

    // --- menu_settings.cpp (m_opties.cpp, m_bedien.cpp) ---------------------
    Task<> menuOpties();                        // 0x407242
    Task<> moOpenScreen();                      // 0x407114
    Task<> moCloseScreen();                     // 0x40718b
    void moCreateForm();                        // 0x4068f2
    void moDrawSettings();                      // 0x406e14
    void moDrawVolume();                        // 0x406eb0
    void moDrawTabsAndAll();                    // 0x406fe2
    Task<> moSlideIn();                         // 0x40707b
    Task<> menuBedien();                        // 0x40654d
    Task<> mcOpenScreen();                      // 0x406491
    Task<> mcCloseScreen();                     // 0x406527
    void mcInitDevice();                        // 0x406363
    void mcDrawKeyboardPanel();                 // 0x4061fb
    void mcDrawJoystickPanel();                 // 0x40629a
    void mcDrawWheelPanel();                    // 0x40631e
    void mcDrawKeyBoxes();                      // 0x405c96
    void mcBlankKeyBoxes();                     // 0x405dac
    void mcDialogPrint(std::string_view text);  // 0x405b39
    Task<> mcDialogOpen();                      // 0x405f26
    Task<> mcDialogClose();                     // 0x405f8d
    Task<int> mcWaitKey();                      // 0x405eb2
    Task<bool> mcInstellen();                   // 0x405fec
    Task<> mcSlideIn();                         // 0x4063f8
    void mcCreateForm();

    // --- menu_scores.cpp (m_score, m_dscore, m_stand, m_clas, handle_game) --
    Task<> menuScore(int tab);                  // 0x407de8 (tab -1 = hiscore)
    void msDrawLists(int tab);                  // 0x407b7b
    Task<> menuDisplayScore(int level);         // mds 0x4083f7 (-1 = hiscore)
    Task<> menuDisplayStand(int stage);         // mdt 0x409325
    Task<> menuDisplayClasse();                 // mdc 0x409842
    Task<bool> handleGame();                    // 0x40b288

public:
    // Form tables (static and mutable in the original).
    struct Forms_ {
        FormDef main;           // mm_form 0x432e78
        FormDef start;          // mst_form 0x4331b0
        FormDef newPlayer;      // mns_form 0x4343dc
        FormDef controlsKeyboard; // mc_form 0x4333fc
        FormDef controlsJoystick; // mc_form_joy 0x4334dc
        FormDef options3d;      // mo_form_3D 0x43397c
        FormDef options320;     // mo_form_320 0x433bc8
        FormDef options640;     // mo_form_640 0x433e14
        FormDef score;          // ms_form 0x434140
        FormDef displayScore;   // mds_form 0x434254
        FormDef stand;          // mdt_form 0x4345b8
        FormDef classe;         // mdc_form 0x434624
        FormDef reqWarning;     // reqw_form 0x435898
        FormDef reqVraag;       // reqv_form 0x4358fc
        FormDef reqInput;       // reqi_form 0x4359a8
    };

private:
    static Forms_ makeForms();

    Runtime& runtime_;
    Forms forms_;
    Forms_ formTables_;
    bool first_;

    // Global settings and databases.
    GameOptions options_;
    ControlSettings controls_;
    SlotDatabase slots_;
    ScoreDatabase scores_;
    Infio infio_;
    std::size_t activeSlot_ = 0;
    // Which player list drawSlotRow/printSlots/reselectSlot emulate. The
    // start screen (mst_*) composes rows on the back surface and ellipsises
    // every column; Nieuwe speler (mns_*) draws on the buffer surface and
    // only ellipsises the name.
    bool newPlayerList_ = false;
    bool joystickAvailable_ = false;
    bool soundAvailable_ = true;    // mysnd_state != 1

    // Sound handles.
    int musicHandle_ = SoundSystem::none;  // mm_tune_buffer 0x432fcc

    // Bediening key boxes (0x433748): LINKS, RECHTS, GAS, REM.
    std::array<std::int32_t, ControlSettings::configurableKeys> keyBoxes_{};
};

} // namespace opena2::game
