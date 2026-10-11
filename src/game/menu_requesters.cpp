// reqwarn.cpp, reqvraag.cpp and reqinput.cpp of A2MENU.DAT: the modal
// requesters drawn over the current screen. The text helpers that live with
// them in the original (win_printwrapped, win_bprintf) are in
// menu_program.cpp.

#include "anims.hpp"
#include "keys.hpp"
#include "menu_program.hpp"

#include <string>
#include <string_view>

namespace opena2::game {

namespace {

// Requester window anims (use_req*_cfg_anims): reqw 17, reqv 15, reqi 12.
constexpr int warningWindowAnim = 17;
constexpr int questionWindowAnim = 15;
constexpr int inputWindowAnim = 12;

constexpr int windowX = 151;
constexpr int windowY = 97;
constexpr int messageX = 261;
constexpr int messageY = 122;
constexpr int messageWidth = 205;
// set_font_colors(1, 0, 0x1EFAFF)
constexpr Rgb messageColor = colorRef(0x1EFAFF);

// get_edit_box_text(2, buf, 0x20): EM_GETLINE into the caller's 32 bytes.
constexpr std::size_t inputBufferSize = 32;

// reqi_form: 0 OK, 1 Annuleren, 2 edit box.
constexpr int inputOk = 0;
constexpr int inputCancel = 1;
constexpr int inputEditBox = 2;

} // namespace

// reqw_open (0x40a1d0), reqv_open (0x40a433), reqi_open (0x40a6e7): freeze
// the screen on the back surface, dither it, draw the window and the
// message, then activate the requester's own form.
void MenuProgram::reqOpen(int windowAnim, std::string_view message)
{
    auto& display = runtime_.display;
    runtime_.setMouseShown(false);
    display.copy(Surface::front, Surface::back, 0, 0, Display::width, Display::height);
    display.grid(Surface::front);
    display.sprite(Surface::front, animSprite(windowAnim, 0), windowX, windowY);
    printWrapped(Surface::front, font::requester, messageColor, messageX, messageY, messageWidth, message);
    runtime_.setMouseShown(true);

    // reqi_open also selects font 2 (bk 0, text 0xF0FAFA, opaque) for the
    // edit box; the EditBox overlay draws its own colours.
    if (windowAnim == inputWindowAnim) {
        forms_.activate(formTables_.reqInput);
        forms_.focus(inputEditBox);
    } else if (windowAnim == questionWindowAnim) {
        forms_.activate(formTables_.reqVraag);
        forms_.focus(0);
    } else {
        forms_.activate(formTables_.reqWarning);
        forms_.focus(0);
    }
}

// reqw_close (0x40a2b6), reqv_close (0x40a51f), reqi_close (0x40a7fd).
Task<> MenuProgram::reqClose()
{
    runtime_.setMouseShown(false);
    runtime_.display.copy(Surface::back, Surface::front, 0, 0, Display::width, Display::height);
    runtime_.setMouseShown(true);
    co_await waitCloseKeys();
    forms_.destroy();
}

// req_warning (0x40a308): OK or Esc closes it, without a click sound.
Task<> MenuProgram::reqWarning(std::string message)
{
    reqOpen(warningWindowAnim, message);
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(0);
            ev = 0;
        }
        if (ev == 0) {
            co_await waitCloseKeys();
            break;
        }
        co_await runtime_.frame();
    }
    co_await reqClose();
}

// req_vraag (0x40a571): Ja returns true; Nee and Esc (which focuses Nee)
// return false. Both play click2.wav, which is waited for before closing.
Task<bool> MenuProgram::reqVraag(std::string message)
{
    reqOpen(questionWindowAnim, message);
    int click = SoundSystem::none;
    bool result = false;
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(1);
            ev = 1;
        }
        if (ev == 0 || ev == 1) {
            result = ev == 0;
            sndPlay(click, "click2.wav");
            co_await waitCloseKeys();
            co_await sndWait(click);   // reqv_wait_snd 0x40a401
            break;
        }
        co_await runtime_.frame();
    }
    co_await reqClose();
    co_return result;
}

// req_input (0x40a85e): OK, or Enter polled while no control fired, copies
// the edit box text into `result`; Annuleren or Esc (which focuses
// Annuleren) cancels. An empty text is accepted.
Task<bool> MenuProgram::reqInput(std::string& result, std::string message)
{
    reqOpen(inputWindowAnim, message);
    int click = SoundSystem::none;
    bool accepted = false;
    const auto editText = [this] {
        std::string text = forms_.editText(inputEditBox);
        if (text.size() > inputBufferSize - 1) text.resize(inputBufferSize - 1);
        return text;
    };
    for (;;) {
        if (runtime_.quitRequested) break;
        int ev = forms_.handle();
        if (ev == event::quit) break;
        if (ev == event::escape) {
            forms_.focus(inputCancel);
            ev = inputCancel;
        }
        if (ev == inputOk) {
            sndPlay(click, "click2.wav");
            co_await waitCloseKeys();
            result = editText();
            co_await sndWait(click);   // reqi_wait_snd 0x40a6b5
            accepted = true;
            break;
        }
        if (ev == inputCancel) {
            sndPlay(click, "click2.wav");
            co_await waitCloseKeys();
            co_await sndWait(click);
            break;
        }
        if (runtime_.keyIsPressed(vk::enter)) {
            forms_.focus(inputOk);
            sndPlay(click, "click2.wav");
            result = editText();
            co_await runtime_.waitKeyReleased(vk::enter);
            accepted = true;
            break;
        }
        co_await runtime_.frame();
    }
    co_await reqClose();
    co_return accepted;
}

} // namespace opena2::game
