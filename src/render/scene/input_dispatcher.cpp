#include "render/scene/input_dispatcher.h"

#include "render/scene/node.h"

InputDispatcher::InputDispatcher() = default;

void InputDispatcher::setSceneRoot(Node* root) { m_sceneRoot = root; }

namespace {
  InputArea* resolveInputArea(Node* hit) {
    for (Node* node = hit; node != nullptr; node = node->parent()) {
      auto* area = dynamic_cast<InputArea*>(node);
      if (area != nullptr && area->enabled()) {
        return area;
      }
    }
    return nullptr;
  }
} // namespace

void InputDispatcher::pointerEnter(float x, float y, std::uint32_t serial) {
  if (!m_sceneRoot)
    return;
  m_lastPointerX = x;
  m_lastPointerY = y;
  Node* hit = Node::hitTest(m_sceneRoot, x, y);
  auto* area = resolveInputArea(hit);

  if (m_hoveredArea && m_hoveredArea != area) {
    m_hoveredArea->m_hovered = false;
    m_hoveredArea->m_pressed = false;
    if (m_hoveredArea->m_onLeave)
      m_hoveredArea->m_onLeave();
  }

  if (area && area != m_hoveredArea) {
    InputArea::PointerData data{x, y, 0, false};
    area->m_hovered = true;
    if (area->m_onEnter)
      area->m_onEnter(data);
    if (m_cursorShapeCallback && area->m_cursorShape != 0) {
      m_cursorShapeCallback(serial, area->m_cursorShape);
    }
  }

  m_hoveredArea = area;
}

void InputDispatcher::pointerLeave() {
  m_capturedArea = nullptr;
  if (m_hoveredArea) {
    m_hoveredArea->m_hovered = false;
    m_hoveredArea->m_pressed = false;
    if (m_hoveredArea->m_onLeave)
      m_hoveredArea->m_onLeave();
    m_hoveredArea = nullptr;
  }
}

void InputDispatcher::pointerMotion(float x, float y, std::uint32_t) {
  if (!m_sceneRoot)
    return;
  m_lastPointerX = x;
  m_lastPointerY = y;
  if (m_capturedArea != nullptr && m_capturedArea->enabled()) {
    if (!m_capturedArea->m_pressed || !m_capturedArea->visible() || !m_capturedArea->hitTestVisible()) {
      m_capturedArea = nullptr;
    } else {
      InputArea::PointerData data{x, y, 0, false};
      if (m_capturedArea->m_onMotion) {
        m_capturedArea->m_onMotion(data);
      }
      return;
    }
  }
  if (m_capturedArea != nullptr && !m_capturedArea->enabled()) {
    m_capturedArea = nullptr;
  }
  Node* hit = Node::hitTest(m_sceneRoot, x, y);
  auto* area = resolveInputArea(hit);

  if (area != m_hoveredArea) {
    if (m_hoveredArea) {
      m_hoveredArea->m_hovered = false;
      m_hoveredArea->m_pressed = false;
      if (m_hoveredArea->m_onLeave)
        m_hoveredArea->m_onLeave();
    }
    if (area) {
      area->m_hovered = true;
      if (area->m_onEnter) {
        InputArea::PointerData data{x, y, 0, false};
        area->m_onEnter(data);
      }
    }
    m_hoveredArea = area;
  }

  if (area && area->m_onMotion) {
    InputArea::PointerData data{x, y, 0, false};
    area->m_onMotion(data);
  }
}

void InputDispatcher::pointerButton(float x, float y, std::uint32_t button, bool pressed) {
  if (!m_sceneRoot)
    return;
  m_lastPointerX = x;
  m_lastPointerY = y;
  InputArea* area = m_capturedArea;
  if (area == nullptr || !area->enabled()) {
    Node* hit = Node::hitTest(m_sceneRoot, x, y);
    area = resolveInputArea(hit);
  }

  if (area && area->enabled()) {
    InputArea::PointerData data{x, y, button, pressed};
    if (pressed) {
      area->m_pressed = true;
      m_capturedArea = area;
      if (area->focusable())
        InputArea::setFocused(area);
      if (area->m_onPress)
        area->m_onPress(data);
    } else {
      const bool wasPressed = area->m_pressed;
      area->m_pressed = false;
      if (area->m_onRelease)
        area->m_onRelease(data);
      if (wasPressed && area->m_onClick)
        area->m_onClick(data);
      m_capturedArea = nullptr;
    }
  } else if (!pressed) {
    m_capturedArea = nullptr;
  }
}

void InputDispatcher::pointerAxis(float x, float y, std::uint32_t axis, std::uint32_t, double value, std::int32_t) {
  if (!m_sceneRoot)
    return;
  Node* hit = Node::hitTest(m_sceneRoot, x, y);
  auto* area = dynamic_cast<InputArea*>(hit);

  if (area && area->m_onAxisHandler) {
    InputArea::PointerData data{x, y, 0, false};
    data.axis = axis;
    data.axisValue = value;
    area->m_onAxisHandler(data);
  }
}

void InputDispatcher::keyEvent(
    std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit
) {
  if (!pressed)
    return;
  InputArea* focused = InputArea::getFocused();
  if (focused && focused->enabled() && focused->m_onKeyDown) {
    InputArea::KeyData data{sym, utf32, modifiers, preedit};
    focused->m_onKeyDown(data);
  }
}

void InputDispatcher::setFocus(InputArea* area) { InputArea::setFocused(area); }

void InputDispatcher::setCursorShapeCallback(std::function<void(std::uint32_t, std::uint32_t)> callback) {
  m_cursorShapeCallback = std::move(callback);
}

void InputDispatcher::invalidateTransientPointers() {
  if (m_hoveredArea != nullptr) {
    m_hoveredArea->m_hovered = false;
    m_hoveredArea->m_pressed = false;
    if (m_hoveredArea->m_onLeave) {
      m_hoveredArea->m_onLeave();
    }
  }
  if (m_capturedArea != nullptr) {
    m_capturedArea->m_pressed = false;
  }
  m_hoveredArea = nullptr;
  m_capturedArea = nullptr;
}
