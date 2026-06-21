#include "ui/controls/search_field.h"

#include "render/core/render_styles.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>

namespace {

  const float kIconGap = Style::spaceSm();
  constexpr float kDividerHeight = 1.0f;

} // namespace

SearchField::SearchField() : Node(NodeType::Container) {
  auto icon = std::make_unique<Glyph>();
  icon->setGlyph("search");
  icon->setGlyphSize(Style::fontSizeBody());
  icon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  icon->setHitTestVisible(false);
  m_icon = static_cast<Glyph*>(addChild(std::move(icon)));

  auto input = std::make_unique<Input>();
  input->setFlatStyle(true);
  input->setFontSize(Style::fontSizeBody());
  m_input = static_cast<Input*>(addChild(std::move(input)));

  auto divider = std::make_unique<RectNode>();
  divider->setHitTestVisible(false);
  divider->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Outline, 0.55f),
          .fillMode = FillMode::Solid,
          .radius = 0.0f,
      }
  );
  m_divider = static_cast<RectNode*>(addChild(std::move(divider)));
}

void SearchField::setPlaceholder(std::string_view placeholder) {
  if (m_input != nullptr) {
    m_input->setPlaceholder(placeholder);
  }
}

void SearchField::setValue(std::string_view value) {
  if (m_input != nullptr) {
    m_input->setValue(value);
  }
}

void SearchField::setFontSize(float size) {
  if (m_icon != nullptr) {
    m_icon->setGlyphSize(size);
  }
  if (m_input != nullptr) {
    m_input->setFontSize(size);
  }
}

void SearchField::setControlHeight(float height) {
  m_controlHeight = std::max(1.0f, height);
  if (m_input != nullptr) {
    m_input->setControlHeight(m_controlHeight);
  }
}

void SearchField::setActiveChrome(bool active) {
  if (m_activeChrome == active) {
    return;
  }
  m_activeChrome = active;
  if (m_divider != nullptr) {
    m_divider->setVisible(!active);
  }
  if (m_icon != nullptr) {
    m_icon->setColor(active ? colorForRole(ColorRole::OnSecondary) : colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_input != nullptr) {
    m_input->setFlatOnSecondary(active);
  }
  markLayoutDirty();
}

void SearchField::setOnChange(std::function<void(const std::string&)> callback) {
  if (m_input != nullptr) {
    m_input->setOnChange(std::move(callback));
  }
}

void SearchField::setOnKeyDown(
    std::function<bool(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool preedit)> callback
) {
  if (m_input != nullptr) {
    m_input->setOnKeyDown(std::move(callback));
  }
}

const std::string& SearchField::value() const noexcept {
  static const std::string kEmpty;
  return m_input != nullptr ? m_input->value() : kEmpty;
}

InputArea* SearchField::inputArea() noexcept { return m_input != nullptr ? m_input->inputArea() : nullptr; }

LayoutSize SearchField::doMeasure(Renderer& renderer, const LayoutConstraints& constraints) {
  doLayout(renderer);
  return constraints.constrain({width(), height()});
}

void SearchField::doLayout(Renderer& renderer) {
  const float w = width() > 0.0f ? width() : Style::controlHeight() * 6.0f;
  const float dividerH = m_activeChrome ? 0.0f : kDividerHeight;
  const float h = m_controlHeight + dividerH;
  setSize(w, h);

  float iconW = 0.0f;
  if (m_icon != nullptr) {
    (void)m_icon->measure(renderer);
    const auto metrics = renderer.measureGlyph(m_icon->codepoint(), m_icon->fontSize());
    iconW = metrics.right - metrics.left;
    const float iconY = std::round(m_controlHeight * 0.5f - (metrics.top + metrics.bottom) * 0.5f);
    m_icon->setPosition(0.0f, iconY);
    m_icon->setSize(std::max(iconW, 1.0f), m_controlHeight);
  }

  if (m_input != nullptr) {
    const float inputX = iconW > 0.0f ? iconW + kIconGap : 0.0f;
    const float inputW = std::max(0.0f, w - inputX);
    m_input->setPosition(inputX, 0.0f);
    m_input->setSize(inputW, m_controlHeight);
    m_input->layout(renderer);
  }

  if (m_divider != nullptr) {
    m_divider->setPosition(0.0f, m_controlHeight);
    m_divider->setSize(w, kDividerHeight);
  }
}
