// m_start.cpp (start-up player selection, NSPEL_1.PCX) and m_nsplr.cpp
// ("Nieuwe speler", NSPEL_2.PCX) of A2MENU.DAT.

#include "menu_program.hpp"

#include "anims.hpp"
#include "keys.hpp"

#include <array>
#include <cstdio>
#include <string>

namespace opena2::game {
namespace {

// mst_form control indices.
namespace start_control {
constexpr int selecteren = 0;
constexpr int verwijder = 1;
constexpr int help = 2;
constexpr int einde = 3;
constexpr int firstRow = 4;
} // namespace start_control

// mns_form control indices.
namespace new_control {
constexpr int selecteren = 0;
constexpr int verwijder = 1;
constexpr int annuleren = 2;
constexpr int help = 3;
constexpr int firstRow = 4;
} // namespace new_control

constexpr int rowCount = static_cast<int>(SlotDatabase::slotCount);

// mst_help (0x433354) and mns_help (0x434580).
constexpr std::array<HelpEntry, 3> startHelp{{
    {start_control::selecteren, "MNS_03.wav"},
    {start_control::verwijder, "MNS_04.wav"},
    {start_control::einde, "MNS_05.wav"},
}};
constexpr std::array<HelpEntry, 3> newPlayerHelp{{
    {new_control::selecteren, "MNS_03.wav"},
    {new_control::verwijder, "MNS_04.wav"},
    {new_control::annuleren, "MNS_06.wav"},
}};

// Slot rows (mst_slot_list 0x433060, mns_slot_list 0x43428c) and the list
// rectangle both screens redraw.
constexpr int rowX = 66;
constexpr int rowY = 141;
constexpr int rowPitch = 24;
constexpr int listLeft = 66;
constexpr int listTop = 141;
constexpr int listRight = 571;
constexpr int listBottom = 381;

// Anim 5 (start) / anim 0 (Nieuwe speler): [267 normal row, 266 selected].
constexpr int rowAnim = 5;

constexpr int volumeMuted = -8000;

constexpr const char* namePrompt = "Vul je naam in en wordt lid van de A2 Racing Club!";
constexpr const char* opstartFile = "OPSTART.TMP";

int rowTop(std::size_t slot)
{
    return rowY + rowPitch * static_cast<int>(slot);
}

std::string formatText(const char* format, const std::string& value)
{
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, format, value.c_str());
    return buffer;
}

std::string formatNumber(const char* format, int value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, value);
    return buffer;
}

} // namespace

// mst_draw_slot_info (0x405090) / mst_draw_selected_slot_info (0x404f10),
// mns_draw_slot_info (0x4085cd) / mns_draw_selected_slot_info (0x40845c).
void MenuProgram::drawSlotRow(std::size_t slot, bool selected)
{
    const Surface surface = newPlayerList_ ? Surface::buffer : Surface::back;
    const int x = rowX;
    const int y = rowTop(slot);
    const Rgb color = selected ? colorRef(0xFAFAFA) : colorRef(0x0AFAFF);

    runtime_.display.sprite(surface, animSprite(rowAnim, selected ? 1 : 0), x, y);

    const auto name = formatText("%-16s", slots_.nameText(slot));
    const auto stage = formatText("%-16s", slots_.stageText(slot));
    const auto money = formatNumber("%9d", slots_[slot].money);
    const auto time = formatText("%-6s", slots_.timeText(slot));

    printEllipsis(surface, font::list, color, x + 10, y, 128, name);
    if (newPlayerList_) {
        print(surface, font::list, color, x + 148, y, stage);
        print(surface, font::list, color, x + 254, y, money);
        print(surface, font::list, color, x + 393, y, time);
    } else {
        printEllipsis(surface, font::list, color, x + 148, y, 250, stage);
        printEllipsis(surface, font::list, color, x + 254, y, 250, money);
        printEllipsis(surface, font::list, color, x + 393, y, 128, time);
    }
}

// mst_print_slot (0x40537f) / mns_print_slot (0x40884c).
void MenuProgram::printSlots(std::size_t selected)
{
    auto& display = runtime_.display;
    if (newPlayerList_) {
        for (std::size_t slot = 0; slot < SlotDatabase::slotCount; ++slot) {
            drawSlotRow(slot, false);
        }
        drawSlotRow(selected, true);
        runtime_.setMouseShown(false);
        display.copy(Surface::buffer, Surface::front, listLeft, listTop, listRight, listBottom);
        runtime_.setMouseShown(true);
        return;
    }
    display.copy(Surface::buffer, Surface::back, listLeft, listTop, listRight, listBottom);
    for (std::size_t slot = 0; slot < SlotDatabase::slotCount; ++slot) {
        drawSlotRow(slot, false);
    }
    drawSlotRow(selected, true);
    runtime_.setMouseShown(false);
    display.copy(Surface::back, Surface::front, listLeft, listTop, listRight, listBottom);
    runtime_.setMouseShown(true);
}

// mst_reselect_slot (0x4052e8) / mns_reselect_slot (0x4087f4).
void MenuProgram::reselectSlot(std::size_t oldSlot, std::size_t newSlot)
{
    auto& display = runtime_.display;
    const Surface surface = newPlayerList_ ? Surface::buffer : Surface::back;
    if (!newPlayerList_) {
        display.copy(Surface::front, Surface::back, listLeft, listTop, listRight, listBottom);
    }
    drawSlotRow(oldSlot, false);
    drawSlotRow(newSlot, true);
    runtime_.setMouseShown(false);
    display.copy(surface, Surface::front, listLeft, listTop, listRight, listBottom);
    runtime_.setMouseShown(true);
}

// mst_handle_intro (0x405429): the logo with algintro.wav until it ends or
// Space or Esc is down, then a fade to black.
Task<> MenuProgram::mstHandleIntro(bool show)
{
    if (!show) {
        co_return;
    }
    loadScreen("LOGO256.PCX", true);
    runtime_.setMouseEnabled(false);
    runtime_.display.clear(Surface::front, 0);
    co_await wipe();
    int sound = SoundSystem::none;
    sndPlay(sound, "algintro.wav");
    while (runtime_.sound.busy(sound) && !runtime_.keyIsPressed(vk::space)
           && !runtime_.keyIsPressed(vk::escape) && !runtime_.quitRequested) {
        co_await runtime_.frame();
    }
    // No form exists during the intro: handle_win_msg dispatches the Esc or
    // Space press to the main window, which ignores it, so it never reaches
    // the name requester that may follow.
    runtime_.discardEvents();
    sndStop(sound);
    runtime_.display.clear(Surface::buffer, 0);
    co_await wipe();
    runtime_.setMouseEnabled(true);
}

// mst_open_screen (0x4054d9): fades in from black.
Task<> MenuProgram::mstOpenScreen(std::size_t selected)
{
    newPlayerList_ = false;
    loadScreen("NSPEL_1.PCX", true);
    runtime_.setMouseShown(false);
    runtime_.display.clear(Surface::front, 0);
    co_await wipe();
    runtime_.setMouseShown(true);
    printSlots(selected);
    // Input made before activate_form went to the main window. [binary]
    runtime_.discardEvents();
    forms_.activate(formTables_.start);
}

// mst_close_screen (0x405598): fades to black.
Task<> MenuProgram::mstCloseScreen()
{
    co_await waitCloseKeys();
    forms_.destroy();
    runtime_.display.clear(Surface::buffer, 0);
    co_await wipe();
}

// menu_start (0x4055ed): returns true to go on to the main menu, false to
// quit the game.
Task<bool> MenuProgram::menuStart(bool first)
{
    bool run = true;
    bool quit = false;
    int sound = SoundSystem::none;
    std::string name;

    co_await mstHandleIntro(first && !slots_[slots_.lastSaved()].used);
    std::size_t selected = slots_.lastSaved();
    co_await mstOpenScreen(selected);
    if (first) {
        sndPlay(sound, "MNS_00.wav");
    }

    // Without players the name requester opens at once; a name goes
    // straight on to the main menu.
    if (!slots_[slots_.lastSaved()].used) {
        co_await sndWait(sound);
        sndPlay(sound, "MNS_01.wav");
        if (co_await reqInput(name, namePrompt)) {
            sndStop(sound);
            slots_.create(selected, name, options_, controls_);
            printSlots(selected);
            setActiveSlot(selected);
            run = false;
        } else {
            sndStop(sound);
        }
    }
    if (run) {
        forms_.activate(formTables_.start);
    }

    while (run) {
        if (runtime_.quitRequested) {
            // Window closed: leave like Einde. [guess: the original ignores WM_QUIT here]
            quit = true;
            break;
        }
        const int event = forms_.handle();
        bool select = false;
        if (event == start_control::selecteren) {
            select = true;
        } else if (event == start_control::verwijder) {
            if (slots_[selected].used) {
                sndPlay(sound, "click1.wav");
                co_await sndWait(sound);
                sndSetVolume(musicHandle_, duckedMusicVolume());
                sndPlay(sound, "MNS_02.wav");
                if (co_await reqVraag(formatText("Wil je deze speler '%s' verwijderen?",
                                                 slots_.nameText(selected)))) {
                    slots_.clear(selected);
                }
                sndStop(sound);
                sndSetVolume(musicHandle_, musicVolume());
                selected = slots_.lastSaved();
                printSlots(selected);
            }
            forms_.activate(formTables_.start);
        } else if (event == start_control::help) {
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(sound);
            sndSetVolume(musicHandle_, volumeMuted);
            co_await playHelp(startHelp);
            sndSetVolume(musicHandle_, musicVolume());
        } else if (event == start_control::einde || event == event::escape) {
            if (event == event::escape) {
                forms_.focus(start_control::einde);
            }
            quit = true;
            sndPlay(sound, "click1.wav");
            run = false;
            co_await sndWait(sound);
        } else if (event >= start_control::firstRow && event < start_control::firstRow + rowCount) {
            const auto row = static_cast<std::size_t>(event - start_control::firstRow);
            if (row == selected) {
                // Clicking the selected row again acts as Selecteren.
                forms_.focus(start_control::selecteren);
                select = true;
            } else {
                sndPlay(sound, "click3.wav");
                reselectSlot(selected, row);
                selected = row;
                co_await sndWait(sound);
            }
        } else if (event == event::up || event == event::down) {
            sndPlay(sound, "click3.wav");
            const auto next = event == event::up ? (selected + 9) % SlotDatabase::slotCount
                                                 : (selected + 1) % SlotDatabase::slotCount;
            reselectSlot(selected, next);
            selected = next;
            co_await sndWait(sound);
            co_await runtime_.waitKeyReleased(event == event::up ? vk::up : vk::down);
        }

        if (select) {
            sndPlay(sound, "click1.wav");
            bool chosen = true;
            if (!slots_[selected].used) {
                co_await sndWait(sound);
                sndSetVolume(musicHandle_, duckedMusicVolume());
                sndPlay(sound, "MNS_01.wav");
                if (co_await reqInput(name, namePrompt)) {
                    sndStop(sound);
                    sndSetVolume(musicHandle_, musicVolume());
                    slots_.create(selected, name, options_, controls_);
                    printSlots(selected);
                } else {
                    // The original leaves the (silent) tune ducked here.
                    sndStop(sound);
                    forms_.activate(formTables_.start);
                    chosen = false;
                }
            }
            if (chosen) {
                setActiveSlot(selected);
                run = false;
                co_await sndWait(sound);
            }
        }
        co_await runtime_.frame();
    }

    co_await mstCloseScreen();
    saveSlots();
    co_return !quit;
}

// mns_open_screen (0x4088b8): wipes straight over the main menu.
Task<> MenuProgram::mnsOpenScreen(std::size_t selected)
{
    newPlayerList_ = true;
    loadScreen("NSPEL_2.PCX", true);
    runtime_.setMouseShown(false);
    co_await wipe();
    runtime_.setMouseShown(true);
    printSlots(selected);
    // Input made before activate_form went to the main window. [binary]
    runtime_.discardEvents();
    forms_.activate(formTables_.newPlayer);
}

// mns_close_screen (0x408940): no wipe; the main menu wipes over it.
Task<> MenuProgram::mnsCloseScreen()
{
    co_await waitCloseKeys();
    forms_.destroy();
}

// menu_nieuwesplr (0x408970): returns false when cancelled, which restores
// every create and delete from OPSTART.TMP.
Task<bool> MenuProgram::menuNieuweSpeler()
{
    bool run = true;
    bool cancelled = false;
    int sound = SoundSystem::none;
    std::string name;

    saveSlotsTo(opstartFile);
    std::size_t selected = slots_.lastSaved();
    co_await mnsOpenScreen(selected);

    while (run) {
        if (runtime_.quitRequested) {
            // Window closed: leave like Annuleren. [guess]
            cancelled = true;
            break;
        }
        const int event = forms_.handle();
        bool select = false;
        if (event == new_control::selecteren) {
            select = true;
        } else if (event == new_control::verwijder) {
            if (slots_[selected].used) {
                sndPlay(sound, "click1.wav");
                co_await sndWait(sound);
                sndSetVolume(musicHandle_, duckedMusicVolume());
                sndPlay(sound, "MNS_02.wav");
                if (co_await reqVraag(formatText("Wil je deze speler '%s' verwijderen?",
                                                 slots_.nameText(selected)))) {
                    slots_.clear(selected);
                }
                sndStop(sound);
                sndSetVolume(musicHandle_, musicVolume());
                selected = slots_.lastSaved();
                printSlots(selected);
                co_await sndWait(sound);
            }
            forms_.activate(formTables_.newPlayer);
        } else if (event == new_control::annuleren || event == event::escape) {
            if (event == event::escape) {
                forms_.focus(new_control::annuleren);
            }
            cancelled = true;
            sndPlay(sound, "click1.wav");
            run = false;
            co_await sndWait(sound);
        } else if (event == new_control::help) {
            sndPlay(sound, "click1.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            co_await sndWait(sound);
            sndSetVolume(musicHandle_, volumeMuted);
            co_await playHelp(newPlayerHelp);
            sndSetVolume(musicHandle_, musicVolume());
        } else if (event >= new_control::firstRow && event < new_control::firstRow + rowCount) {
            const auto row = static_cast<std::size_t>(event - new_control::firstRow);
            if (row == selected) {
                forms_.focus(new_control::selecteren);
                select = true;
            } else {
                sndPlay(sound, "click3.wav");
                reselectSlot(selected, row);
                printSlots(row);
                selected = row;
                co_await sndWait(sound);
            }
        } else if (event == event::up || event == event::down) {
            sndPlay(sound, "click3.wav");
            const auto next = event == event::up ? (selected + 9) % SlotDatabase::slotCount
                                                 : (selected + 1) % SlotDatabase::slotCount;
            reselectSlot(selected, next);
            selected = next;
            co_await sndWait(sound);
            co_await runtime_.waitKeyReleased(event == event::up ? vk::up : vk::down);
        }

        if (select) {
            sndPlay(sound, "click1.wav");
            bool chosen = true;
            if (!slots_[selected].used) {
                co_await sndWait(sound);
                sndSetVolume(musicHandle_, duckedMusicVolume());
                sndPlay(sound, "MNS_01.wav");
                const bool entered = co_await reqInput(name, namePrompt);
                sndStop(sound);
                sndSetVolume(musicHandle_, musicVolume());
                if (entered) {
                    slots_.create(selected, name, options_, controls_);
                    printSlots(selected);
                } else {
                    forms_.activate(formTables_.newPlayer);
                    chosen = false;
                }
            }
            if (chosen) {
                setActiveSlot(selected);
                run = false;
                co_await sndWait(sound);
            }
        }
        co_await runtime_.frame();
    }

    co_await mnsCloseScreen();
    newPlayerList_ = false;
    if (cancelled) {
        loadSlotsFrom(opstartFile);
    }
    saveSlots();
    co_return !cancelled;
}

} // namespace opena2::game
