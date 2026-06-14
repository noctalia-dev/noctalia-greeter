#include "render/scene/input_area.h"

InputArea* InputArea::s_focused = nullptr;

InputArea::InputArea() : Node(NodeType::InputArea) {}

InputArea::~InputArea() {
  if (s_focused == this) {
    s_focused = nullptr;
  }
}

void InputArea::setFocusable(bool focusable) { m_focusable = focusable; }
void InputArea::setEnabled(bool enabled) { m_enabled = enabled; }
void InputArea::setCursorShape(std::uint32_t shape) { m_cursorShape = shape; }

void InputArea::setOnEnter(std::function<void(const PointerData&)> callback) { m_onEnter = std::move(callback); }
void InputArea::setOnLeave(std::function<void()> callback) { m_onLeave = std::move(callback); }
void InputArea::setOnPress(std::function<void(const PointerData&)> callback) { m_onPress = std::move(callback); }
void InputArea::setOnRelease(std::function<void(const PointerData&)> callback) { m_onRelease = std::move(callback); }
void InputArea::setOnClick(std::function<void(const PointerData&)> callback) { m_onClick = std::move(callback); }
void InputArea::setOnMotion(std::function<void(const PointerData&)> callback) { m_onMotion = std::move(callback); }
void InputArea::setOnKeyDown(std::function<void(const KeyData&)> callback) { m_onKeyDown = std::move(callback); }
void InputArea::setOnAxisHandler(std::function<bool(const PointerData&)> callback) {
  m_onAxisHandler = std::move(callback);
}
void InputArea::setOnFocusChange(std::function<void(bool)> callback) { m_onFocusChange = std::move(callback); }

void InputArea::focus() {
  if (m_focused)
    return;
  m_focused = true;
  s_focused = this;
  markPaintDirty();
  if (m_onFocusChange)
    m_onFocusChange(true);
}

void InputArea::blur() {
  if (!m_focused)
    return;
  m_focused = false;
  if (s_focused == this)
    s_focused = nullptr;
  markPaintDirty();
  if (m_onFocusChange)
    m_onFocusChange(false);
}

void InputArea::setFocused(InputArea* area) {
  if (s_focused && s_focused != area) {
    s_focused->blur();
  }
  if (area) {
    area->focus();
  }
}

InputArea* InputArea::getFocused() { return s_focused; }
