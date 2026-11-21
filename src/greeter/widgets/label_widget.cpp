#include "greeter/widgets/label_widget.h"

#include "render/core/renderer.h"
#include "ui/controls/label.h"
#include "ui/style.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace greeter {
  namespace {
    constexpr float kMaxWidth = 320.0F;
    constexpr float kShadowAlpha = 0.6F;
    constexpr float kShadowOffset = 1.5F;
  } // namespace

  LabelWidget::LabelWidget(const Options& options) : m_opacity(options.opacity), m_shadow(options.shadow) {
    setHitTestVisible(false);
    auto title = std::make_unique<Label>();
    m_title = title.get();
    title->setText(options.title);
    title->setFontWeight(FontWeight::Bold);
    title->setMaxLines(3);
    addChild(std::move(title));

    auto description = std::make_unique<Label>();
    m_description = description.get();
    description->setText(options.description);
    description->setMaxLines(0);
    description->setVisible(!options.description.empty());
    addChild(std::move(description));

    for (auto* label : {m_title, m_description}) {
      label->setFontFamily(options.fontFamily);
      label->setTextAlign(TextAlign::Start);
      label->setUseMarkup(false);
      label->setColor(scaleAlpha(options.color, options.opacity));
    }
  }

  void LabelWidget::layoutAtScale(Renderer& renderer, float scale) {
    const float shadowPad = m_shadow ? kShadowOffset * scale : 0.0F;
    const auto shadowColor = colorSpecFromRole(ColorRole::Shadow, kShadowAlpha * m_opacity);
    for (auto* label : {m_title, m_description}) {
      label->setMaxWidth(kMaxWidth * scale);
      if (m_shadow)
        label->setShadow(shadowColor, shadowPad, shadowPad);
      else
        label->clearShadow();
    }
    m_title->setFontSize(Style::fontSizeBody * 1.5F * scale);
    m_title->measure(renderer);
    m_title->setPosition(0.0F, 0.0F);
    float width = m_title->width();
    float height = m_title->height();
    if (m_description->visible()) {
      m_description->setFontSize(Style::fontSizeBody * scale);
      m_description->measure(renderer);
      m_description->setPosition(0.0F, height);
      width = std::max(width, m_description->width());
      height += m_description->height();
    }
    setSize(width + shadowPad, height + shadowPad);
  }

} // namespace greeter
