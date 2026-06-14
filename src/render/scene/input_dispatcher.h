#pragma once

#include "render/scene/input_area.h"

#include <cstdint>
#include <functional>

class Node;

class InputDispatcher {
public:
  InputDispatcher();

  void setSceneRoot(Node* root);

  void pointerEnter(float x, float y, std::uint32_t serial);
  void pointerLeave();
  void pointerMotion(float x, float y, std::uint32_t serial);
  void pointerButton(float x, float y, std::uint32_t button, bool pressed);
  void pointerAxis(float x, float y, std::uint32_t axis, std::uint32_t axisSource, double value, std::int32_t discrete);
  void keyEvent(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit);

  void setFocus(InputArea* area);
  void setCursorShapeCallback(std::function<void(std::uint32_t serial, std::uint32_t shape)> callback);
  void invalidateTransientPointers();

private:
  Node* m_sceneRoot = nullptr;
  InputArea* m_hoveredArea = nullptr;
  InputArea* m_capturedArea = nullptr;
  float m_lastPointerX = 0.0f;
  float m_lastPointerY = 0.0f;
  std::function<void(std::uint32_t serial, std::uint32_t shape)> m_cursorShapeCallback;
};
