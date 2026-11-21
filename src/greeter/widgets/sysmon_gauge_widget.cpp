#include "greeter/widgets/sysmon_gauge_widget.h"

#include "core/ui_phase.h"
#include "render/core/renderer.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/progress_bar.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace greeter {

  SysmonGaugeWidget::SysmonGaugeWidget(const Options& options)
      : m_metric(options.metric), m_color(options.color), m_highlightColor(options.highlightColor),
        m_stacked(options.stacked), m_labelMinWidth(options.labelMinWidth) {
    setHitTestVisible(false);
    auto glyph = std::make_unique<Glyph>();
    m_glyph = glyph.get();
    glyph->setGlyph(sysmonMetricDefinitions()[static_cast<std::size_t>(m_metric)].glyph);
    if (options.shadow)
      glyph->setShadow(colorSpecFromRole(ColorRole::Shadow, 0.5F), 0, 1);
    addChild(std::move(glyph));
    auto gauge = std::make_unique<ProgressBar>();
    m_gauge = gauge.get();
    addChild(std::move(gauge));
    if (options.showLabel) {
      auto label = std::make_unique<Label>();
      m_label = label.get();
      label->setText("--");
      label->setFontFamily(options.fontFamily);
      label->setFontWeight(FontWeight::Medium);
      label->setUseMarkup(false);
      if (options.shadow)
        label->setShadow(colorSpecFromRole(ColorRole::Shadow, 0.5F), 0, 1);
      addChild(std::move(label));
    }
    syncColor();
    syncProgress();
    m_paletteConnection = paletteChanged().connect([this] { syncColor(); });
  }

  bool SysmonGaugeWidget::updateMetrics(const SysmonSnapshot& snapshot) {
    uiAssertNotRendering("SysmonGaugeWidget::updateMetrics");
    const auto& reading = snapshot.reading(m_metric);
    if (m_current == reading.current && m_reading == reading.gauge)
      return false;
    m_current = reading.current;
    m_reading = reading.gauge;
    if (m_label != nullptr)
      m_label->setText(formatSysmonValue(sysmonMetricDefinitions()[static_cast<std::size_t>(m_metric)], m_current));
    syncProgress();
    syncColor();
    return true;
  }

  void SysmonGaugeWidget::syncColor() {
    const Color color = lerpHsvChromaWeighted(
        resolveColorSpec(m_color), resolveColorSpec(m_highlightColor), m_reading ? m_reading->highlight : 0.0F
    );
    m_glyph->setColor(color);
    if (m_label != nullptr)
      m_label->setColor(color);
    m_gauge->setFill(color);
    m_gauge->setTrack(scaleAlpha(m_color, 0.3F));
  }

  void SysmonGaugeWidget::syncProgress() {
    const float normalized = m_reading ? m_reading->normalized : 0.0F;
    const float axis = m_stacked ? m_gauge->width() : m_gauge->height();
    m_gauge->setProgress(normalized * axis < 1.0F ? 0.0F : normalized);
  }

  void SysmonGaugeWidget::layoutAtScale(Renderer& renderer, float scale) {
    const float gap = Style::spaceXs * scale;
    m_glyph->setGlyphSize(Style::baseGlyphSize * scale);
    m_glyph->measure(renderer);
    if (m_label != nullptr) {
      m_label->setFontSize((m_stacked ? Style::fontSizeCaption : Style::fontSizeBody) * scale);
      m_label->setMinWidth(m_labelMinWidth * scale);
      m_label->measure(renderer);
    }
    const float labelWidth = m_label != nullptr ? m_label->width() : 0.0F;
    const float labelHeight = m_label != nullptr ? m_label->height() : 0.0F;
    const float stem = std::round(Style::fontSizeBody * scale * 0.85F);
    const float thickness = std::max(3.0F, std::round(Style::fontSizeBody * scale * 0.3F));
    const auto& definition = sysmonMetricDefinitions()[static_cast<std::size_t>(m_metric)];
    const auto metrics = renderer.measureGlyph(GlyphRegistry::lookup(definition.glyph), Style::baseGlyphSize * scale);
    const float inkWidth = metrics.right - metrics.left;
    const float inkLeft = (m_glyph->width() - inkWidth) * 0.5F;
    m_gauge->setRadius(thickness * 0.5F);
    if (m_stacked) {
      const float trackWidth = std::max(inkWidth, stem);
      const float width = std::max(trackWidth, labelWidth);
      m_glyph->setPosition(std::round((width - inkWidth) * 0.5F - inkLeft), 0);
      m_gauge->setOrientation(ProgressBarOrientation::Horizontal);
      m_gauge->setPosition(std::round((width - trackWidth) * 0.5F), m_glyph->height() + gap);
      m_gauge->setSize(trackWidth, thickness);
      float height = m_glyph->height() + gap + thickness;
      if (m_label != nullptr) {
        m_label->setPosition((width - labelWidth) * 0.5F, height + gap);
        height += gap + labelHeight;
      }
      setSize(width, height);
    } else {
      const float height = std::max({m_glyph->height(), stem, labelHeight});
      const float glyphX = std::round(std::max(0.0F, -inkLeft));
      const float gaugeX = std::round(glyphX + inkLeft + inkWidth + gap);
      m_glyph->setPosition(glyphX, std::round((height - m_glyph->height()) * 0.5F));
      m_gauge->setOrientation(ProgressBarOrientation::Vertical);
      m_gauge->setPosition(gaugeX, std::round((height - stem) * 0.5F));
      m_gauge->setSize(thickness, stem);
      float width = gaugeX + thickness;
      if (m_label != nullptr) {
        m_label->setPosition(width + gap, (height - labelHeight) * 0.5F);
        width += gap + labelWidth;
      }
      setSize(width, height);
    }
    syncProgress();
  }

} // namespace greeter
