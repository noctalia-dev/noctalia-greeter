#pragma once

#include "render/scene/node.h"
#include "render/scene/rect_node.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

class Glyph;
class Input;
class InputArea;

class SearchField : public Node {
public:
  SearchField();

  void setPlaceholder(std::string_view placeholder);
  void setValue(std::string_view value);
  void setFontSize(float size);
  void setControlHeight(float height);
  void setActiveChrome(bool active);

  void setOnChange(std::function<void(const std::string&)> callback);
  void setOnKeyDown(
      std::function<bool(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool preedit)> callback
  );

  [[nodiscard]] const std::string& value() const noexcept;
  [[nodiscard]] InputArea* inputArea() noexcept;

private:
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void doLayout(Renderer& renderer) override;

  Glyph* m_icon = nullptr;
  Input* m_input = nullptr;
  RectNode* m_divider = nullptr;
  float m_controlHeight = 36.0f;
  bool m_activeChrome = false;
};
