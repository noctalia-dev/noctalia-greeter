#include "greeter/widgets/button_widget.h"

#include "ui/style.h"

#include <algorithm>
#include <button_widget.h>
#include <cmath>
#include <memory>

namespace greeter {

  ButtonWidget::ButtonWidget(const Options& options) {
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
    // Presentation only; action dispatch remains owned by the greeter.
    button->setTabStop(false);
    addChild(std::move(button));
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
