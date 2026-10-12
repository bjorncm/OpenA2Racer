// m_score.cpp (Score), m_dscore.cpp (Score2/Score3), m_stand.cpp (EtappeN
// result), m_clas.cpp (TStand/TStand2 standings) and handle_game (spel.cpp)
// of A2MENU.DAT.

#include "anims.hpp"
#include "keys.hpp"
#include "menu_program.hpp"
#include "opena2/race/race_state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace opena2::game {

namespace {

// List colours (COLORREF): yellow text, red new entry or player row.
constexpr Rgb normalColor = colorRef(0x0AFAFF);
constexpr Rgb scoreNewColor = colorRef(0x0A0FFA);   // Score screen
constexpr Rgb highlightColor = colorRef(0x0A0FFF);  // Score2/3, EtappeN, TStand
constexpr Rgb shadowColor = colorRef(0x000000);

// Every list is drawn three times: two black shadows, then the text.
struct Pass {
    int dx;
    int dy;
    bool shadow;
};
constexpr std::array<Pass, 3> passes{{{2, 2, true}, {1, 1, true}, {0, 0, false}}};

// Score form (ms_form 0x434140): 0 Terug, 1 Totaal, 2..7 Etappe 1..6.
constexpr int scoreBack = 0;
constexpr int scoreTotalTab = 1;
constexpr int scoreFirstStageTab = 2;
constexpr int scoreLastStageTab = 7;

// Anim 80: tabs (frames 0..5 Etappe 1..6, 6 Totaal); anim 81: headers
// (0 total, 1 stage).
constexpr int tabAnim = 80;
constexpr int headerAnim = 81;

// ms_get_current_display (0x4078dc) / mds_get_current_display (0x407f2c).
int clampList(int list)
{
    return list >= 0 && list < stageCount ? list : -1;
}

std::string formatNumber(const char* format, int value)
{
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}

Rgb rowColor(const Pass& pass, bool highlighted, Rgb highlight)
{
    if (pass.shadow) return shadowColor;
    return highlighted ? highlight : normalColor;
}

// mdc_stand_list entry (0x437f30, 9 bytes).
struct Standing {
    std::int32_t total = 0;
    std::string name;
    bool isPlayer = false;
};

} // namespace

// ms_display_score (0x407b7b): restores the list area from the background,
// draws the list on the back surface, puts the tab and header sprites on
// the screen and copies the list to the screen.
void MenuProgram::msDrawLists(int tab)
{
    auto& display = runtime_.display;
    constexpr int left = 44;
    constexpr int top = 177;
    constexpr int right = 604;
    constexpr int bottom = 407;
    constexpr int pitch = 23;
    display.copy(Surface::buffer, Surface::back, left, top, right, bottom);

    int tabFrame = 6;
    int headerFrame = 0;
    if (tab == -1) {
        // ms_print_hiscore (0x407a2f)
        for (const auto& pass : passes) {
            const auto& list = scores_.hiscore();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& entry = list[i];
                const Rgb color = rowColor(pass, entry.isNew != 0, scoreNewColor);
                const int y = top + pass.dy + pitch * static_cast<int>(i);
                const int x = pass.dx;
                display.text(Surface::back, font::list, color, x + 44, y,
                             formatNumber("%d", static_cast<int>(i) + 1));
                printEllipsis(Surface::back, font::list, color, x + 82, y, 132, entry.name);
                display.text(Surface::back, font::list, color, x + 224, y, stageName(entry.stage));
                display.text(Surface::back, font::list, color, x + 354, y, formatNumber("%9d", entry.money));
                display.text(Surface::back, font::list, color, x + 484, y, formatRaceTime(entry.time));
            }
        }
    } else if (tab >= 0 && tab < stageCount) {
        // ms_print_score (0x407910)
        for (const auto& pass : passes) {
            const auto& list = scores_.stage(tab);
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& entry = list[i];
                const Rgb color = rowColor(pass, entry.isNew != 0, scoreNewColor);
                const int y = top + pass.dy + pitch * static_cast<int>(i);
                const int x = pass.dx;
                display.text(Surface::back, font::list, color, x + 44, y,
                             formatNumber("%2d", static_cast<int>(i) + 1));
                printEllipsis(Surface::back, font::list, color, x + 82, y, 176, entry.name);
                display.text(Surface::back, font::list, color, x + 268, y, formatNumber("%9d", entry.money));
                display.text(Surface::back, font::list, color, x + 484, y, formatRaceTime(entry.time));
            }
        }
        tabFrame = tab;
        headerFrame = 1;
    }

    display.sprite(Surface::front, animSprite(tabAnim, tabFrame), 0, 112);
    display.sprite(Surface::front, animSprite(headerAnim, headerFrame), 0, 153);
    display.copy(Surface::back, Surface::front, left, top, right, bottom);
}

// menu_score (0x407de8). `tab` 0..5 opens a stage list, anything else the
// overall hiscore. Tabs switch on their area buttons (Alt+0..6) only.
Task<> MenuProgram::menuScore(int tab)
{
    int current = clampList(tab);

    // ms_open_screen (0x407d33). Score.pcx is loaded with its palette
    // (flags 0x1F) and its palette is set again after the wipe.
    loadScreen("SCORE.PCX", true);
    co_await wipe();
    msDrawLists(current);
    forms_.activate(formTables_.score);
    forms_.focus(scoreBack);

    int click = SoundSystem::none;
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(scoreBack);
            ev = scoreBack;
        }
        if (ev == scoreBack) {
            sndPlay(click, "click1.wav");
            co_await sndWait(click);
            break;
        }
        int wanted = current;
        if (ev == scoreTotalTab) {
            wanted = -1;
        } else if (ev >= scoreFirstStageTab && ev <= scoreLastStageTab) {
            wanted = clampList(ev - scoreFirstStageTab);
        }
        if (wanted != current) {
            sndPlay(click, "click3.wav");
            co_await forms_.waitHotkeyReleased();
            co_await runtime_.waitKeyReleased(vk::enter);
            current = wanted;
            msDrawLists(current);
            co_await sndWait(click);
        }
        co_await runtime_.frame();
    }

    // ms_close_screen (0x407dc2): no wipe, the next screen fades over it.
    co_await waitCloseKeys();
    forms_.destroy();
}

// menu_display_score (0x4083f7): Score2.pcx with the stage list `level`
// after a race, or Score3.pcx with the hiscore (-1) after the last stage.
Task<> MenuProgram::menuDisplayScore(int level)
{
    auto& display = runtime_.display;
    const int list = clampList(level);
    constexpr int left = 44;
    constexpr int top = 155;
    constexpr int right = 624;
    constexpr int bottom = 405;
    constexpr int pitch = 25;

    // mds_open_screen (0x408312)
    loadScreen(list == -1 ? "SCORE3.PCX" : "SCORE2.PCX", true);
    display.clear(Surface::front, 0);
    co_await wipe();

    // mds_display_score (0x4081c0)
    display.copy(Surface::buffer, Surface::back, left, top, right, bottom);
    for (const auto& pass : passes) {
        const auto& entries = list == -1 ? scores_.hiscore() : scores_.stage(list);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            const Rgb color = rowColor(pass, entry.isNew != 0, highlightColor);
            const int y = top + pass.dy + pitch * static_cast<int>(i);
            const int x = pass.dx;
            display.text(Surface::back, font::list, color, x + 44, y,
                         formatNumber("%2d", static_cast<int>(i) + 1));
            printEllipsis(Surface::back, font::list, color, x + 82, y, 132, entry.name);
            if (list == -1) {
                // mds_print_hiscore (0x40808e)
                display.text(Surface::back, font::list, color, x + 224, y, stageName(entry.stage));
            } else {
                // mds_print_score (0x407f60)
                printEllipsis(Surface::back, font::list, color, x + 268, y, 150,
                              formatNumber("%9d", entry.money));
            }
            display.text(Surface::back, font::list, color, x + 484, y, formatRaceTime(entry.time));
        }
    }
    display.copy(Surface::back, Surface::front, left, top, right, bottom);
    forms_.activate(formTables_.displayScore);
    forms_.focus(0);

    int click = SoundSystem::none;
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(0);
            ev = 0;
        }
        if (ev == 0) {
            sndPlay(click, "click1.wav");
            co_await sndWait(click);
            break;
        }
        co_await runtime_.frame();
    }

    // mds_close_screen (0x4083ac): fade to black.
    co_await waitCloseKeys();
    forms_.destroy();
    display.clear(Surface::buffer, 0);
    co_await wipe();
}

// menu_display_stand (0x409325): the result of the stage just driven.
Task<> MenuProgram::menuDisplayStand(int stage)
{
    auto& display = runtime_.display;
    constexpr int left = 44;
    constexpr int top = 155;
    constexpr int right = 624;
    constexpr int bottom = 405;
    constexpr int pitch = 25;

    // mdt_open_screen (0x409243): mdt_stand_pcx[stage % 6].
    const int background = ((stage % stageCount) + stageCount) % stageCount;
    loadScreen("ETAPPE" + std::to_string(background + 1) + ".PCX", true);
    display.clear(Surface::front, 0);
    co_await wipe();

    // mdt_display_score (0x4091ad) and mdt_display_stand (0x408d7c). Row p
    // shows whoever finished at position p, checked in the order player,
    // racer 1..5 (the last match wins). The row is red when the player
    // matched, even if a racer with the same position overrides the name.
    display.copy(Surface::buffer, Surface::back, left, top, right, bottom);
    if (stage >= 0 && stage < stageCount) {
        const auto& slot = slots_[activeSlot_];
        const auto s = static_cast<std::size_t>(stage);
        for (const auto& pass : passes) {
            for (int position = 1; position <= 6; ++position) {
                bool player = false;
                std::string name;
                std::int32_t time = 0;
                if (slot.playerPosition[s] == position) {
                    player = true;
                    name = slot.name;
                    time = slot.playerTime[s];
                }
                for (std::size_t k = 0; k < computerRacerCount; ++k) {
                    if (slot.racerPosition[k][s] == position) {
                        name = computerRacerNames()[k];
                        time = slot.racerTime[k][s];
                    }
                }
                const Rgb color = rowColor(pass, player, highlightColor);
                const int y = top + pass.dy + pitch * (position - 1);
                const int x = pass.dx;
                display.text(Surface::back, font::list, color, x + 44, y, formatNumber("%d", position));
                display.text(Surface::back, font::list, color, x + 82, y, name);
                display.text(Surface::back, font::list, color, x + 484, y, formatRaceTime(time));
            }
        }
    }
    display.copy(Surface::back, Surface::front, left, top, right, bottom);
    forms_.activate(formTables_.stand);
    forms_.focus(0);

    int click = SoundSystem::none;
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(0);
            ev = 0;
        }
        if (ev == 0) {
            sndPlay(click, "click1.wav");
            co_await sndWait(click);
            break;
        }
        co_await runtime_.frame();
    }

    // mdt_close_screen (0x4092da): fade to black.
    co_await waitCloseKeys();
    forms_.destroy();
    display.clear(Surface::buffer, 0);
    co_await wipe();
}

// menu_display_classe (0x409842): the overall standings after the stages
// driven so far; TStand2.pcx once the last stage is done.
Task<> MenuProgram::menuDisplayClasse()
{
    auto& display = runtime_.display;
    constexpr int left = 44;
    constexpr int top = 155;
    constexpr int right = 624;
    constexpr int bottom = 405;
    constexpr int pitch = 25;
    const auto& slot = slots_[activeSlot_];

    // mdc_open_screen (0x409739)
    loadScreen(slot.stage >= stageCount ? "TSTAND2.PCX" : "TSTAND.PCX", true);
    display.clear(Surface::front, 0);
    co_await wipe();

    // mdc_create_huidige_stand (0x4093b3): total times over the driven
    // stages, sorted ascending, ties by name (mdc_compare_score 0x409384).
    std::array<Standing, computerRacerCount + 1> standings;
    standings[0] = {slot.playerTotal(slot.stage), slot.name, true};
    for (std::size_t k = 0; k < computerRacerCount; ++k) {
        standings[k + 1] = {slot.racerTotal(static_cast<int>(k), slot.stage), computerRacerNames()[k], false};
    }
    std::sort(standings.begin(), standings.end(), [](const Standing& a, const Standing& b) {
        if (a.total != b.total) return a.total < b.total;
        return std::strncmp(a.name.c_str(), b.name.c_str(), 32) < 0;
    });

    // mdc_display_score (0x4096a8) and mdc_display_huidige_stand (0x40956e).
    display.copy(Surface::buffer, Surface::back, left, top, right, bottom);
    for (const auto& pass : passes) {
        for (std::size_t i = 0; i < standings.size(); ++i) {
            const auto& standing = standings[i];
            const Rgb color = rowColor(pass, standing.isPlayer, highlightColor);
            const int y = top + pass.dy + pitch * static_cast<int>(i);
            const int x = pass.dx;
            display.text(Surface::back, font::list, color, x + 44, y,
                         formatNumber("%d", static_cast<int>(i) + 1));
            display.text(Surface::back, font::list, color, x + 82, y, standing.name);
            display.text(Surface::back, font::list, color, x + 484, y, formatRaceTime(standing.total));
        }
    }
    display.copy(Surface::back, Surface::front, left, top, right, bottom);
    forms_.activate(formTables_.classe);
    forms_.focus(0);

    int click = SoundSystem::none;
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(0);
            ev = 0;
        }
        if (ev == 0) {
            sndPlay(click, "click1.wav");
            co_await sndWait(click);
            break;
        }
        co_await runtime_.frame();
    }

    // mdc_close_screen (0x4097f7): fade to black.
    co_await waitCloseKeys();
    forms_.destroy();
    display.clear(Surface::buffer, 0);
    co_await wipe();
}

// handle_game (0x40b288): stores the race results from INFIO.INI in the
// active slot and shows the post-race screens. Returns false after the last
// stage (the original deletes the career and shows the start screen),
// true otherwise (main menu).
Task<bool> MenuProgram::handleGame()
{
    activeSlot_ = static_cast<std::size_t>(
        std::clamp<int>(infio_.activeSlot, 0, static_cast<int>(SlotDatabase::slotCount) - 1));
    // EINDE_DOOR 0..3 all lead to the main menu (jump table 0x40b278).
    if (!infio_.raceFinished) co_return true;
    infio_.raceFinished = 0;

    auto& slot = slots_[activeSlot_];
    const int stage = slot.stage;
    // slot_check_level is fatal in the original for anything but 0..5.
    if (stage < 0 || stage >= stageCount) co_return true;
    const auto s = static_cast<std::size_t>(stage);
    ++slot.stage;
    // Capped at what the race's four-digit money display shows; the race
    // also replaces a larger amount with 1000 at the start. Not in the
    // original.
    slot.money = std::clamp(infio_.money, 0, race::maxMoney);
    slot.playerTime[s] = infio_.time;
    slot.playerPosition[s] = infio_.position;
    for (std::size_t k = 0; k < computerRacerCount; ++k) {
        slot.racerPosition[k][s] = infio_.racerPosition[k];
        slot.racerTime[k][s] = infio_.racerTime[k];
    }

    int rock = SoundSystem::none;
    sndPlayLooping(rock, "rock.wav");
    co_await menuDisplayStand(stage);
    co_await menuDisplayClasse();
    const int stagePlace = scores_.addStageScore(slot.name, slot.playerTime[s], stage, slot.money);
    if (stagePlace >= 0 && stagePlace < static_cast<int>(ScoreDatabase::entriesPerList)) {
        co_await menuDisplayScore(stage);
    }
    // while (snd_fade_out(&h)): the first step already ends at or below
    // zero attenuation, so the music simply stops (0x40bad8).
    sndStop(rock);

    if (slot.stage < stageCount) co_return true;

    // Last stage: fade to black, offer the overall hiscore. The original
    // deletes the finished career here; this version starts it again at the
    // first stage with the money won, and returns to the main menu.
    runtime_.display.clear(Surface::buffer, 0);
    co_await wipe();
    runtime_.display.clear(Surface::back, 0);
    const int place = scores_.addHiscore(slot.name, slot.playerTotal(slot.stage), slot.stage, slot.money);
    if (place >= 0 && place < static_cast<int>(ScoreDatabase::entriesPerList)) {
        co_await menuDisplayScore(-1);
    }
    slots_.restartCareer(activeSlot_);
    co_return true;
}

} // namespace opena2::game
