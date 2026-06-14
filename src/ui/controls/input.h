#pragma once

#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class GlyphNode;
class Label;
class Node;

class Input : public Node {
public:
  Input();

  void setValue(std::string_view value);
  void setPlaceholder(std::string_view placeholder);
  void setFontSize(float size);
  void setControlHeight(float height);
  void setPasswordMode(bool enabled);
  void setInvalid(bool invalid);
  void setEnabled(bool enabled);
  void setBold(bool bold);

  void setOnChange(std::function<void(const std::string&)> callback);
  void setOnSubmit(std::function<void(const std::string&)> callback);
  void setOnFocusLoss(std::function<void()> callback);

  void selectAll();

  [[nodiscard]] const std::string& value() const noexcept { return m_value; }
  [[nodiscard]] InputArea* inputArea() noexcept { return m_inputArea; }

private:
  void doLayout(Renderer& renderer) override;
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void applyVisualState();
  void updateDisplayText();
  void updateInteractiveGeometry();
  void handleKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool preedit);
  void syncPasswordGlyphNodes(std::size_t count);
  void deleteSelection();
  void ensureCursorVisible();
  void clampScrollOffset();

  [[nodiscard]] bool hasSelection() const noexcept;
  [[nodiscard]] std::size_t selectionStart() const noexcept;
  [[nodiscard]] std::size_t selectionEnd() const noexcept;
  [[nodiscard]] float textViewportWidth() const noexcept;
  [[nodiscard]] float stopXForByte(std::size_t bytePos) const;
  [[nodiscard]] std::size_t xToByteOffset(float localX) const;

  static std::size_t nextCharPos(const std::string& s, std::size_t pos);
  static std::size_t prevCharPos(const std::string& s, std::size_t pos);
  static std::string utf32ToUtf8(std::uint32_t codepoint);

  RectNode* m_background = nullptr;
  Node* m_textViewport = nullptr;
  RectNode* m_selectionRect = nullptr;
  Label* m_label = nullptr;
  RectNode* m_cursor = nullptr;
  InputArea* m_inputArea = nullptr;

  std::string m_value;
  std::string m_placeholder;
  std::size_t m_cursorPos = 0;
  std::size_t m_selectionAnchor = 0;

  std::vector<float> m_stopX;
  std::vector<std::size_t> m_stopByte;
  std::vector<GlyphNode*> m_passwordGlyphs;
  float m_scrollOffset = 0.0f;

  float m_fontSize = 14.0f;
  float m_controlHeight = 36.0f;
  bool m_passwordMode = false;
  bool m_invalid = false;
  bool m_enabled = true;

  std::function<void(const std::string&)> m_onChange;
  std::function<void(const std::string&)> m_onSubmit;
  std::function<void()> m_onFocusLoss;
};
