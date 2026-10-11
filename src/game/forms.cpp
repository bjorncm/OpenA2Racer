#include "forms.hpp"

#include "anims.hpp"
#include "keys.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace opena2::game {

namespace {

// focus_anim timer: the original advances it every 500th handle_form call
// (form_anim_timer 0x435f6c), which depends on the CPU. [guess] 40 ms.
constexpr std::uint64_t focusAnimInterval = 40;
// Default Windows caret blink time.
constexpr std::uint64_t caretBlink = 530;
// EDIT's text margin; must match Display::drawEditBox. [guess]
constexpr int editMargin = 2;
constexpr int editFont = 2;

// allowed_edit_keys default (A2MENU.DAT 0x42fb94, copied by
// clear_edit_keys_allowed 0x417f39). The string ends in "'" and a space,
// so both are allowed.
constexpr std::string_view allowedEditKeys =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890.<,/?![]%$&*+-=>:;' ";

} // namespace

Forms::Forms(Runtime& runtime) : runtime_(runtime) {}

ControlDef& Forms::control(int index)
{
    return form_->controls[static_cast<std::size_t>(index)];
}

const ControlDef& Forms::control(int index) const
{
    return form_->controls[static_cast<std::size_t>(index)];
}

int Forms::controlCount() const noexcept
{
    return form_ ? static_cast<int>(form_->controls.size()) : 0;
}

namespace {

// w/h of a control; -1 means the size of frame 0 of its anim (create_button
// 0x41ad0a, create_areabutton 0x41b63a).
struct Size {
    int width;
    int height;
};

Size controlSize(const Display& display, const ControlDef& def)
{
    Size size{def.width, def.height};
    const int sprite = def.anim >= 0 ? animSprite(def.anim, 0) : -1;
    if (size.width == -1 && sprite >= 0) size.width = display.spriteWidth(sprite);
    if (size.height == -1 && sprite >= 0) size.height = display.spriteHeight(sprite);
    return size;
}

// set_mem_image with flag 1 at (x + w/2, y + h/2): A2MRC hotspots are the
// sprite centres, so this centres the sprite on the control.
void drawCentred(Display& display, int sprite, const ControlDef& def, Size size)
{
    if (sprite < 0) return;
    display.sprite(Surface::front, sprite, def.x + size.width / 2 - display.spriteWidth(sprite) / 2,
                   def.y + size.height / 2 - display.spriteHeight(sprite) / 2);
}

} // namespace

// activate_form (0x417c4a)
void Forms::activate(FormDef& form)
{
    if (form_) destroy();
    selected_ = event::none;
    form_ = &form;
    focus_ = -1;
    const auto count = form.controls.size();
    live_.assign(count, Live{});
    lastFocusAnim_ = runtime_.now();

    for (int i = 0; i < controlCount(); ++i) {
        auto& def = control(i);
        int value = def.stateValue >= 0 ? def.stateValue : def.initialState;
        switch (def.type) {
        case ControlType::button:
            // create_button (0x41ad0a): focus anim frame 0, state 4 becomes 1.
            live_[static_cast<std::size_t>(i)].focusFrame = 0;
            if (value == state::select) value = state::focus;
            break;
        case ControlType::area:
            // create_areabutton (0x41b63a) blits frame 0 of its anim once.
            if (def.anim >= 0) {
                drawCentred(runtime_.display, animSprite(def.anim, 0), def,
                            controlSize(runtime_.display, def));
            }
            break;
        case ControlType::edit:
            // create_edit_box (0x4180c2): WM_SETTEXT leaves the caret at 0.
            editText_ = def.text;
            caret_ = 0;
            break;
        }
        setState(i, value);
    }

    // The first WM_PAINT of the owner-draw buttons (draw_button 0x41aeef).
    for (int i = 0; i < controlCount(); ++i) {
        if (control(i).type == ControlType::button) redrawButton(i);
    }
    updateEditOverlay();
}

// destroy_form (0x417bd4). The windows go away but their pixels stay on the
// front surface. Stored states are kept for the next activate.
void Forms::destroy()
{
    form_ = nullptr;
    live_.clear();
    focus_ = -1;
    runtime_.display.editBox.visible = false;
}

// handle_form (0x417e8e)
int Forms::handle()
{
    if (!form_) return runtime_.quitRequested ? event::quit : event::none;
    selected_ = event::none;
    lastHotkey_ = -1;

    if (runtime_.now() - lastFocusAnim_ >= focusAnimInterval) {
        lastFocusAnim_ = runtime_.now();
        tickFocusAnim();
    }

    // A captured BUTTON follows the mouse: leaving its rect releases the
    // pushed look, coming back pushes (and so selects) it again.
    for (int i = 0; i < controlCount() && selected_ == event::none; ++i) {
        if (!live_[static_cast<std::size_t>(i)].pressed) continue;
        const auto& def = control(i);
        const Size size = controlSize(runtime_.display, def);
        const float mx = runtime_.mouseX();
        const float my = runtime_.mouseY();
        const bool inside = mx >= static_cast<float>(def.x) && mx < static_cast<float>(def.x + size.width)
                            && my >= static_cast<float>(def.y) && my < static_cast<float>(def.y + size.height);
        const bool pushed = def.stateValue == state::select;
        if (inside && !pushed) {
            press(i);
        } else if (!inside && pushed) {
            drawButton(i, 3);
            control(i).stateValue = state::focus;
        }
    }

    // handle_win_msg. [deviation] Dispatching stops after a selection so
    // that input batched into one frame is not lost; the rest is handled by
    // the next call.
    InputEvent event;
    while (form_ && selected_ == event::none && runtime_.popEvent(event)) {
        switch (event.type) {
        case InputEvent::Type::mouseDown: {
            if (event.vk != vk::leftButton) break;
            const int index = controlAt(event.x, event.y);
            // Disabled windows get no mouse input.
            if (index < 0 || live_[static_cast<std::size_t>(index)].disabled) break;
            auto& def = control(index);
            if (def.type == ControlType::button) {
                // BUTTON's WM_LBUTTONDOWN: SetFocus, SetCapture, push.
                setFocus(index);
                live_[static_cast<std::size_t>(index)].pressed = true;
                press(index);
            } else if (def.type == ControlType::area) {
                // AreaButtonWindowProc (0x41b81d): selected on press, no focus.
                setState(index, state::select);
            } else {
                // EDIT: focus and put the caret at the nearest character.
                setFocus(index);
                if (focus_ == index) {
                    const float local = event.x - static_cast<float>(def.x + editMargin);
                    std::size_t best = 0;
                    float bestDistance = std::abs(local);
                    for (std::size_t k = 1; k <= editText_.size(); ++k) {
                        const float width = static_cast<float>(
                            runtime_.display.textWidth(editFont, std::string_view(editText_).substr(0, k)));
                        if (std::abs(local - width) < bestDistance) {
                            bestDistance = std::abs(local - width);
                            best = k;
                        }
                    }
                    caret_ = best;
                    caretEpoch_ = runtime_.now();
                }
            }
            break;
        }
        case InputEvent::Type::mouseUp:
            if (event.vk != vk::leftButton) break;
            for (int i = 0; i < controlCount(); ++i) {
                auto& live = live_[static_cast<std::size_t>(i)];
                if (!live.pressed) continue;
                live.pressed = false;
                // WM_LBUTTONUP: a pushed button is redrawn with ODS_FOCUS.
                if (control(i).stateValue == state::select && focus_ == i) {
                    drawButton(i, 3);
                    control(i).stateValue = state::focus;
                }
            }
            break;
        case InputEvent::Type::keyDown:
            handleKeyDown(event);
            break;
        case InputEvent::Type::keyUp:
            // ButtonWindowProc WM_KEYUP Enter: redraw with the focus state.
            if (event.vk == vk::enter && !event.alt && focus_ >= 0
                && control(focus_).type == ControlType::button) {
                redrawButton(focus_);
            }
            break;
        case InputEvent::Type::text:
            handleText(event);
            break;
        }
    }

    updateEditOverlay();
    if (runtime_.quitRequested) selected_ = event::quit;
    return selected_;
}

// set_control_state (0x417404)
void Forms::setState(int index, int value)
{
    if (!form_ || index < 0 || index >= controlCount()) return;
    auto& def = control(index);
    def.stateValue = value;
    switch (value) {
    case state::focus:
        // The original aborts for an areabutton; setFocus passes it on.
        if (focus_ != index) setFocus(index);
        break;
    case state::disable:
    case state::enable: {
        // EnableWindow repaints only when the enabled state changes; a
        // window losing its enabled state also loses the focus.
        const bool disable = value == state::disable;
        bool& disabled = live_[static_cast<std::size_t>(index)].disabled;
        if (disabled == disable) break;
        disabled = disable;
        if (disable && focus_ == index) {
            focus_ = -1;
            live_[static_cast<std::size_t>(index)].pressed = false;
        }
        if (def.type == ControlType::button) redrawButton(index);
        break;
    }
    case state::select:
        selected_ = index;
        break;
    default:
        break;
    }
}

// focus_control (0x41762a)
void Forms::focus(int index)
{
    setFocus(index);
    updateEditOverlay();
}

bool Forms::canFocus(int index) const
{
    return !live_[static_cast<std::size_t>(index)].disabled && control(index).type != ControlType::area;
}

// SetFocus on a control window, with the repaints Windows sends.
void Forms::setFocus(int index)
{
    if (!form_ || index < 0 || index >= controlCount()) return;
    // SetFocus fails on a disabled window.
    if (live_[static_cast<std::size_t>(index)].disabled) return;
    if (focus_ == index) return;

    const int old = focus_;
    focus_ = index;
    if (old >= 0) {
        auto& previous = control(old);
        live_[static_cast<std::size_t>(old)].pressed = false;
        if (previous.type == ControlType::button) {
            // WM_KILLFOCUS: redrawn with itemState 0.
            drawButton(old, 0);
            previous.stateValue = state::normal;
        }
    }

    auto& def = control(index);
    if (def.type == ControlType::area) {
        // AreaButtonWindowProc WM_SETFOCUS: on to the next control, or the
        // previous one while Shift is held.
        moveFocus(runtime_.keyDown(vk::shift) ? -1 : 1);
        if (focus_ == index) focus_ = -1;
        return;
    }
    if (def.type == ControlType::button) {
        // WM_SETFOCUS repaints with ODS_FOCUS.
        drawButton(index, 3);
        def.stateValue = state::focus;
    } else {
        caretEpoch_ = runtime_.now();
    }
}

// set_next_control_focus / set_previous_control_focus (0x417387 / 0x4173bd).
// Areabuttons pass the focus on in the same direction, so they are skipped.
void Forms::moveFocus(int direction)
{
    const int count = controlCount();
    if (count == 0) return;
    int i = focus_;
    for (int tries = 0; tries < count; ++tries) {
        if (direction > 0) {
            i = (i < 0 || i >= count - 1) ? 0 : i + 1;
        } else {
            i = (i <= 0) ? count - 1 : i - 1;
        }
        if (i == focus_) return;
        if (canFocus(i)) {
            setFocus(i);
            return;
        }
    }
}

void Forms::drawButton(int index, int frame)
{
    const auto& def = control(index);
    drawCentred(runtime_.display, animSprite(def.anim, frame), def, controlSize(runtime_.display, def));
}

// draw_button (0x41aeef) for a repaint: disabled -> frame 2 (state 2),
// focused -> frame 3 (state 1), else frame 0 (state 0).
void Forms::redrawButton(int index)
{
    auto& def = control(index);
    if (live_[static_cast<std::size_t>(index)].disabled) {
        drawButton(index, 2);
        def.stateValue = state::disable;
    } else if (focus_ == index) {
        drawButton(index, 3);
        def.stateValue = state::focus;
    } else {
        drawButton(index, 0);
        def.stateValue = state::normal;
    }
}

// draw_button with itemState 0x11: the pressed frame, and the action.
void Forms::press(int index)
{
    drawButton(index, 1);
    setState(index, state::select);
}

int Forms::controlAt(float x, float y) const
{
    // [guess] The first created control is on top.
    for (int i = 0; i < controlCount(); ++i) {
        const auto& def = control(i);
        const Size size = controlSize(runtime_.display, def);
        if (x >= static_cast<float>(def.x) && x < static_cast<float>(def.x + size.width)
            && y >= static_cast<float>(def.y) && y < static_cast<float>(def.y + size.height)) {
            return i;
        }
    }
    return -1;
}

// check_form_key_pressed (0x417d3e)
void Forms::checkFormKeyPressed(const InputEvent& event)
{
    // keyispressed(VK_MENU): Alt down and Shift up.
    if (event.alt && !event.shift) {
        for (int i = 0; i < controlCount(); ++i) {
            const auto& def = control(i);
            // Only buttons and areabuttons have a check_hotkey. It does not
            // look at the enabled state.
            if (def.type == ControlType::edit || def.hotkey == 0 || def.hotkey != event.vk) continue;
            lastHotkey_ = event.vk;
            setState(i, state::select);
            setFocus(i);
            // WM_DRAWITEM itemState 0x11 (swallowed by areabuttons).
            if (def.type == ControlType::button) press(i);
            return;
        }
        return;
    }
    switch (event.vk) {
    case vk::escape: selected_ = event::escape; break;
    case vk::up: selected_ = event::up; break;
    case vk::down: selected_ = event::down; break;
    case vk::left: selected_ = event::left; break;
    case vk::right: selected_ = event::right; break;
    default: break;
    }
}

// WM_KEYDOWN / WM_SYSKEYDOWN of the focused control. Without a focused
// control the keys reach the main window, which ignores them.
void Forms::handleKeyDown(const InputEvent& event)
{
    const int target = focus_;
    if (target < 0) return;
    checkFormKeyPressed(event);
    // WM_SYSKEYDOWN goes no further.
    if (event.alt || !form_ || target >= controlCount()) return;

    switch (control(target).type) {
    case ControlType::button:
        // ButtonWindowProc (0x41afe2): every other key is swallowed.
        if (event.vk == vk::tab) {
            moveFocus(event.shift ? -1 : 1);
        } else if (event.vk == vk::enter) {
            press(target);
        }
        break;
    case ControlType::edit:
        // EditWindowProc (0x4183a7)
        if (event.vk == vk::tab) {
            moveFocus(event.shift ? -1 : 1);
        } else {
            handleEditKey(target, event);
        }
        break;
    case ControlType::area:
        break;
    }
}

// The single-line EDIT's own key handling (no selection support).
void Forms::handleEditKey(int index, const InputEvent& event)
{
    (void)index;
    switch (event.vk) {
    case vk::left:
    case vk::up:   // single-line EDIT treats Up like Left
        if (caret_ > 0) --caret_;
        break;
    case vk::right:
    case vk::down:
        if (caret_ < editText_.size()) ++caret_;
        break;
    case vk::home:
        caret_ = 0;
        break;
    case vk::end:
        caret_ = editText_.size();
        break;
    case vk::del:
        if (caret_ < editText_.size()) editText_.erase(caret_, 1);
        break;
    case vk::backspace:
        // WM_CHAR 0x08 always passes the filter.
        if (caret_ > 0) {
            editText_.erase(caret_ - 1, 1);
            --caret_;
        }
        break;
    default:
        return;
    }
    caretEpoch_ = runtime_.now();
}

// WM_CHAR to the focused edit box, filtered by allowed_edit_keys. Without
// ES_AUTOHSCROLL, EDIT refuses characters that would not fit its width.
void Forms::handleText(const InputEvent& event)
{
    if (focus_ < 0 || control(focus_).type != ControlType::edit) return;
    // Alt+key is WM_SYSCHAR, which EDIT does not insert.
    if (event.alt || runtime_.keyDown(vk::alt)) return;
    const auto& def = control(focus_);
    const int room = controlSize(runtime_.display, def).width - 2 * editMargin;
    for (const char c : event.text) {
        if (c == '\0' || allowedEditKeys.find(c) == std::string_view::npos) continue;
        std::string next = editText_;
        next.insert(caret_, 1, c);
        if (runtime_.display.textWidth(editFont, next) > room) continue;
        editText_ = std::move(next);
        ++caret_;
        caretEpoch_ = runtime_.now();
    }
}

void Forms::updateEditOverlay()
{
    auto& box = runtime_.display.editBox;
    box.visible = false;
    for (int i = 0; i < controlCount(); ++i) {
        const auto& def = control(i);
        if (def.type != ControlType::edit) continue;
        const Size size = controlSize(runtime_.display, def);
        box.visible = true;
        box.x = def.x;
        box.y = def.y;
        box.width = size.width;
        box.height = size.height;
        box.font = editFont;
        box.text = editText_;
        box.caret = std::min(caret_, editText_.size());
        box.focused = focus_ == i;
        box.caretVisible = ((runtime_.now() - caretEpoch_) / caretBlink) % 2 == 0;
        return;
    }
}

// draw_form_animfocus_buttons (0x417cf6) -> draw_focus_anim_button (0x41ac3e)
void Forms::tickFocusAnim()
{
    if (focus_ < 0 || control(focus_).type != ControlType::button) return;
    const auto& def = control(focus_);
    if (def.focusAnim < 0) return;
    auto& frame = live_[static_cast<std::size_t>(focus_)].focusFrame;
    drawCentred(runtime_.display, animSprite(def.focusAnim, frame), def, controlSize(runtime_.display, def));
    frame = frame < animFrameCount(def.focusAnim) - 1 ? frame + 1 : 0;
}

// get_edit_box_text (0x417f93)
std::string Forms::editText(int index) const
{
    if (!form_ || index < 0 || index >= controlCount() || control(index).type != ControlType::edit) {
        return {};
    }
    return editText_;
}

// wait_form_key_released (0x417f09): keeps pumping messages while the last
// Alt+hotkey is held; whatever they select is dropped.
Task<> Forms::waitHotkeyReleased()
{
    const int key = lastHotkey_;
    while (key != -1 && !runtime_.quitRequested && runtime_.keyIsPressed(key)) {
        co_await runtime_.frame();
        handle();
    }
}

// call_help_function (0x417b1c)
void Forms::drawHelp(int index)
{
    if (!form_ || index < 0 || index >= controlCount()) return;
    const auto& def = control(index);
    if (def.type == ControlType::button) {
        // draw_help_button (0x41ae63)
        const int frame = def.stateValue == state::focus || def.stateValue == state::select ? 1 : 0;
        drawButton(index, frame);
        return;
    }
    // draw_help_areabutton (0x41b79e) / draw_help_editbox (0x418035)
    const Size size = controlSize(runtime_.display, def);
    runtime_.display.copy(Surface::buffer, Surface::front, def.x, def.y, def.x + size.width,
                          def.y + size.height);
}

} // namespace opena2::game
