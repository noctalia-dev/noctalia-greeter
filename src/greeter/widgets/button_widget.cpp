#include "greeter/widgets/button_widget.h"

#include "ui/style.h"

#include <algorithm>
#include <button_widget.h>
#include <cmath>
#include <memory>
#include <utility>

namespace greeter {

  ButtonWidget::ButtonWidget(const Options& options, WidgetActions actions)
      : m_action(options.action), m_actions(std::move(actions)) {
    setHitTestVisible(false);
    auto button = std::make_unique<::DesktopButton>();
    m_button = button.get();
    button->setOptions({
        .glyph = options.glyph,
        .label = options.label,
        .variant = options.variant,
        .showBackground = options.background,
        .labelColor = options.color,
        .hoverBackground = options.hoverBackground,
    });
    button->setFontFamily(options.fontFamily);
    button->setEditorPreview(true);
    button->setTabStop(false);
    if (m_action != WidgetAction::None && m_actions.available && m_actions.activate) {
      button->setOnClick([this] {
        if (!m_active || !m_actions.available(m_action))
          return;
        for (const Node* node = this; node != nullptr; node = node->parent())
          if (!node->visible())
            return;
        m_actions.activate(m_action, *this);
      });
    }
    addChild(std::move(button));
  }

  void ButtonWidget::setActive(bool active) {
    m_active = active
        && m_action != WidgetAction::None
        && m_actions.available
        && m_actions.activate
        && m_actions.available(m_action);
    setHitTestVisible(m_active);
    m_button->setEditorPreview(!m_active);
    m_button->setTabStop(m_active);
  }

  void ButtonWidget::layoutButton(Renderer& renderer, float scale, float width, float height) {
    m_button->setContentScale(scale);
    m_button->setFillSize(width, height);
    m_button->layout(renderer);
    setSize(m_button->width(), m_button->height());
  }

  void ButtonWidget::layoutAtScale(Renderer& renderer, float scale) { layoutButton(renderer, scale); }

  bool ButtonWidget::layoutInBox(Renderer& renderer, float width, float height) {
    constexpr float kMinFit = 0.05F;
    constexpr float kMaxFit = 20.0F;
    float scale = std::clamp(height / Style::controlHeight, kMinFit, kMaxFit);
    for (int attempt = 0; attempt < 4; ++attempt) {
      layoutAtScale(renderer, scale);
      if (m_button->width() <= width + 0.5F)
        break;
      const float adjusted = std::clamp(scale * width / std::max(1.0F, m_button->width()), kMinFit, kMaxFit);
      if (std::abs(adjusted - scale) < 0.001F)
        break;
      scale = adjusted;
    }
    layoutButton(renderer, scale, width, height);
    return true;
  }

} // namespace greeter
