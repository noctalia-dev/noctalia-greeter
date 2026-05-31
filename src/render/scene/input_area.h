#pragma once

#include "render/scene/node.h"

#include <cstdint>
#include <functional>

class InputArea : public Node {
public:
  struct PointerData {
    float localX = 0.0f;
    float localY = 0.0f;
    std::uint32_t button = 0;
    bool pressed = false;
    std::uint32_t axis = 0;
    double axisValue = 0.0;
  };

  struct KeyData {
    std::uint32_t sym = 0;
    std::uint32_t utf32 = 0;
    std::uint32_t modifiers = 0;
    bool preedit = false;
  };

  InputArea();
  ~InputArea() override;

  void setFocusable(bool focusable);
  void setEnabled(bool enabled);
  void setCursorShape(std::uint32_t shape);

  void setOnEnter(std::function<void(const PointerData&)> callback);
  void setOnLeave(std::function<void()> callback);
  void setOnPress(std::function<void(const PointerData&)> callback);
  void setOnRelease(std::function<void(const PointerData&)> callback);
  void setOnClick(std::function<void(const PointerData&)> callback);
  void setOnMotion(std::function<void(const PointerData&)> callback);
  void setOnKeyDown(std::function<void(const KeyData&)> callback);
  void setOnAxisHandler(std::function<bool(const PointerData&)> callback);
  void setOnFocusChange(std::function<void(bool focused)> callback);

  void focus();
  void blur();

  [[nodiscard]] bool focused() const noexcept { return m_focused; }
  [[nodiscard]] bool hovered() const noexcept { return m_hovered; }
  [[nodiscard]] bool pressed() const noexcept { return m_pressed; }
  [[nodiscard]] bool focusable() const noexcept { return m_focusable; }
  [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

  static void setFocused(InputArea* area);
  static InputArea* getFocused();

  friend class InputDispatcher;

private:
  bool m_focusable = false;
  bool m_enabled = true;
  bool m_focused = false;
  bool m_hovered = false;
  bool m_pressed = false;
  std::uint32_t m_cursorShape = 0;

  std::function<void(const PointerData&)> m_onEnter;
  std::function<void()> m_onLeave;
  std::function<void(const PointerData&)> m_onPress;
  std::function<void(const PointerData&)> m_onRelease;
  std::function<void(const PointerData&)> m_onClick;
  std::function<void(const PointerData&)> m_onMotion;
  std::function<void(const KeyData&)> m_onKeyDown;
  std::function<bool(const PointerData&)> m_onAxisHandler;
  std::function<void(bool)> m_onFocusChange;

  static InputArea* s_focused;
};
