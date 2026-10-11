#pragma once

#include "runtime.hpp"
#include "task.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opena2::game {

// forms.cpp / buttons.cpp / areabuts.cpp / editbox.cpp of A2MENU.DAT.
//
// Controls behave like the original Win32 child windows:
// - button (type 1): 4-frame anim [normal, pressed, disabled, focused],
//   drawn with set_mem_image onto the front surface whenever its state
//   changes. Fires on press (mouse-down, Enter key-down, Alt+hotkey). The
//   highlight is keyboard focus, not mouse-over. An optional focus anim is
//   advanced on the focused button by handle_form.
// - areabutton (type 4): invisible hit rectangle, fires on mouse-down,
//   cannot hold focus (focus passes to the next control).
// - edit box (type 0): single-line EDIT, white on black, white on red while
//   focused, filtered characters, limited by its pixel width.
enum class ControlType { edit = 0, button = 1, area = 4 };

// Control states (set_control_state).
namespace state {
constexpr int normal = 0;
constexpr int focus = 1;
constexpr int disable = 2;
constexpr int enable = 3;
constexpr int select = 4;
} // namespace state

struct ControlDef {
    int x = 0;
    int y = 0;
    ControlType type = ControlType::button;
    int anim = -1;        // button anim, or areabutton anim (-1 = invisible)
    int width = -1;       // -1 = width of the anim's first sprite
    int height = -1;
    int hotkey = 0;       // virtual-key code, works as Alt+key
    int focusAnim = -1;   // looping highlight anim of the focused button
    int initialState = state::normal;
    std::string text;     // edit box initial text
    int maxChars = 0;     // edit box field (only checked for > 0)

    // Runtime state written back like the original's static tables.
    int stateValue = -1;
};

// A form table. Its state survives destroy/activate like the original's
// static form_t arrays.
struct FormDef {
    std::vector<ControlDef> controls;
};

// One help step: the control lit while `wav` plays.
struct HelpEntry {
    int control = 0;
    const char* wav = nullptr;
};

// handle_form results besides control indices.
namespace event {
constexpr int none = -1;
constexpr int quit = -2;
constexpr int escape = 10000;
constexpr int up = 10001;
constexpr int down = 10002;
constexpr int left = 10003;
constexpr int right = 10004;
} // namespace event

class Forms {
public:
    explicit Forms(Runtime& runtime);

    // activate_form (0x417c4a): destroys the active form and creates every
    // control, applying each control's stored state.
    void activate(FormDef& form);
    // destroy_form (0x417bd4).
    void destroy();
    bool active() const noexcept { return form_ != nullptr; }

    // handle_form (0x417e8e): one call per loop iteration. Processes queued
    // input and returns the activated control, an event:: code, or none.
    int handle();

    void setState(int index, int value);
    void enable(int index) { setState(index, state::enable); }
    void disable(int index) { setState(index, state::disable); }
    // focus_control (0x41762a).
    void focus(int index);
    // get_control_focus: focused control index or -1.
    int focused() const noexcept { return focus_; }
    int controlCount() const noexcept;

    // get_edit_box_text (0x417f93).
    std::string editText(int index) const;

    // The virtual key of the last Alt+hotkey (control_keypressed).
    int lastHotkey() const noexcept { return lastHotkey_; }
    // wait_form_key_released (0x417f09).
    Task<> waitHotkeyReleased();

    // draw_help_* of a control (call_help_function 0x417b1c).
    void drawHelp(int index);

private:
    struct Live {
        int focusFrame = 0;
        bool pressed = false;   // mouse captured after a press
        bool disabled = false;  // EnableWindow(FALSE) state
    };

    ControlDef& control(int index);
    const ControlDef& control(int index) const;
    int controlAt(float x, float y) const;
    bool canFocus(int index) const;
    void setFocus(int index);
    void moveFocus(int direction);
    void drawButton(int index, int frame);
    void redrawButton(int index);
    void press(int index);
    void checkFormKeyPressed(const InputEvent& event);
    void handleKeyDown(const InputEvent& event);
    void handleEditKey(int index, const InputEvent& event);
    void handleText(const InputEvent& event);
    void updateEditOverlay();
    void tickFocusAnim();

    Runtime& runtime_;
    FormDef* form_ = nullptr;
    std::vector<Live> live_;
    int focus_ = -1;
    int selected_ = event::none;
    int lastHotkey_ = -1;
    std::uint64_t lastFocusAnim_ = 0;
    // Edit box state (only one edit box exists in the menu).
    std::string editText_;
    std::size_t caret_ = 0;
    std::uint64_t caretEpoch_ = 0;   // caret blink phase start
};

} // namespace opena2::game
