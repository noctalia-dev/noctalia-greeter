#include "greeter/widgets/digital_clock_widget.h"

#include "render/core/renderer.h"
#include "time/time_format.h"
#include "ui/controls/label.h"
#include "ui/style.h"

#include <memory>
#include <utility>

namespace greeter {

  DigitalClockWidget::DigitalClockWidget(Options options)
      : m_format(std::move(options.format)), m_timezone(std::move(options.timezone)), m_shadow(options.shadow),
        m_centerText(options.centerText) {
    setHitTestVisible(false);
    auto label = std::make_unique<Label>();
    m_label = label.get();
    label->setFontFamily(std::move(options.fontFamily));
    label->setFontWeight(FontWeight::Bold);
    label->setFontFeatures("tnum");
    label->setColor(options.color);
    label->setTextAlign(m_centerText ? TextAlign::Center : TextAlign::Start);
    label->setUseMarkup(false);
    label->setMaxLines(0);
    addChild(std::move(label));
    updateTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  }

  std::optional<std::chrono::seconds> DigitalClockWidget::tickInterval() const {
    if (m_format.empty())
      return std::nullopt;
    return std::chrono::seconds{formatShowsSeconds(m_format) ? 1 : 60};
  }

  bool DigitalClockWidget::updateTime(std::chrono::sys_seconds now) {
    return m_label->setText(formatTimezoneUnixTime(now.time_since_epoch().count(), m_format, m_timezone));
  }

  void DigitalClockWidget::layoutAtScale(Renderer& renderer, float scale) {
    const float fontSize = Style::fontSizeBody * 4.0F * scale;
    const auto& font = m_label->font();
    const bool metricsChanged = m_metricsFontSize != fontSize
        || m_metricsRenderScale != renderer.renderScale()
        || m_metricsGeneration != renderer.textMetricsGeneration();
    if (metricsChanged) {
      m_metricsFontSize = fontSize;
      m_metricsRenderScale = renderer.renderScale();
      m_metricsGeneration = renderer.textMetricsGeneration();
      float widest = -1.0F;
      for (char digit = '0'; digit <= '9'; ++digit) {
        const float width = renderer.measureText(std::string(1, digit), fontSize, font).width;
        if (width > widest) {
          widest = width;
          m_widestDigit = digit;
        }
      }
    }
    std::string sample = m_label->text();
    for (char& ch : sample)
      if (ch >= '0' && ch <= '9')
        ch = m_widestDigit;
    if (metricsChanged || sample != m_sample) {
      m_sample = std::move(sample);
      m_stableWidth = renderer.measureText(m_sample, fontSize, font).width;
    }
    m_label->setFontSize(fontSize);
    m_label->setMinWidth(m_stableWidth);
    m_label->setMaxWidth(m_stableWidth);
    const float shadowPad = m_shadow ? 1.5F * scale : 0.0F;
    if (m_shadow)
      m_label->setShadow(colorSpecFromRole(ColorRole::Shadow, 0.6F), shadowPad, shadowPad);
    else
      m_label->clearShadow();
    m_label->measure(renderer);
    m_label->setPosition(0.0F, 0.0F);
    setSize(m_label->width() + shadowPad, m_label->height() + shadowPad);
  }

} // namespace greeter
