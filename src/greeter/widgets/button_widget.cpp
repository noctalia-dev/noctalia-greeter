#include "greeter/widgets/button_widget.h"

#include "ui/controls/label.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace greeter {

  ButtonWidget::ButtonWidget(const Options& options)
      : m_hasLabel(!options.label.empty()), m_hasGlyph(!options.glyph.empty()) {
    setHitTestVisible(false);
    auto button = std::make_unique<Button>();
    m_button = button.get();
    button->setText(options.label);
    if (m_hasGlyph)
      button->setGlyph(options.glyph);
    if (m_hasLabel) {
      button->label()->setFontFamily(options.fontFamily);
      button->label()->setUseMarkup(false);
    }
    button->setVariant(options.variant);
    auto colors = Button::defaultPalette(options.variant);
    if (options.color) {
      colors.normal.label = *options.color;
      colors.pressed.label = *options.color;
      colors.disabled.label = *options.color;
      if (colors.selected)
        colors.selected->label = *options.color;
    }
    colors.hover.bg = options.hoverBackground;
    if (!options.background) {
      const auto stripChrome = [](Button::ButtonStateColors& state) {
        state.bg = clearColorSpec();
        state.border = clearColorSpec();
      };
      colors.borderWidth = 0;
      stripChrome(colors.normal);
      stripChrome(colors.pressed);
      stripChrome(colors.disabled);
      if (colors.selected)
        stripChrome(*colors.selected);
    }
    button->setCustomPalette(colors);
    // Presentation only: no callback or action provider is bound at this checkpoint.
    button->setTabStop(false);
    addChild(std::move(button));
  }

  void ButtonWidget::layoutButton(Renderer& renderer, float scale, float width, float height) {
    m_button->setSize(0, 0);
    m_button->setMinWidth(width);
    m_button->setMinHeight(height > 0 ? height : Style::controlHeight * scale);
    if (m_hasLabel)
      m_button->setFontSize(Style::fontSizeBody * scale);
    if (m_hasGlyph)
      m_button->setGlyphSize(Style::baseGlyphSize * scale);
    m_button->setPadding(Style::spaceSm * scale, Style::spaceMd * scale);
    m_button->setGap(Style::spaceXs * scale);
    m_button->setStyleRadius(Style::radiusMd, scale);
    m_button->layout(renderer);
    if (width > 0 && height > 0)
      m_button->setSize(width, height);
    m_button->updateInputArea();
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
