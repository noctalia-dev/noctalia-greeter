#include "greeter/widgets/sysmon_graph_widget.h"

#include "core/ui_phase.h"
#include "greeter/render/sysmon_graph_plot.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace greeter {

  SysmonGraphWidget::SysmonGraphWidget(const Options& options)
      : m_metrics{options.primary, options.secondary.value_or(options.primary)}, m_count(options.secondary ? 2 : 1) {
    setHitTestVisible(false);
    auto plot = std::make_unique<SysmonGraphPlot>(options.color, options.color2);
    m_plot = plot.get();
    addChild(std::move(plot));
    for (std::size_t i = 0; i < m_count; ++i) {
      const auto& definition = sysmonMetricDefinitions()[static_cast<std::size_t>(m_metrics[i])];
      const auto color = i == 0 ? options.color : options.color2;
      auto glyph = std::make_unique<Glyph>();
      m_glyphs[i] = glyph.get();
      glyph->setGlyph(definition.glyph);
      glyph->setColor(color);
      if (options.shadow)
        glyph->setShadow(colorSpecFromRole(ColorRole::Shadow, 0.5F), 0, 1);
      addChild(std::move(glyph));
      if (options.showLabel) {
        auto label = std::make_unique<Label>();
        m_labels[i] = label.get();
        label->setText("--");
        label->setFontFamily(options.fontFamily);
        label->setFontWeight(FontWeight::Medium);
        label->setColor(color);
        label->setUseMarkup(false);
        if (options.shadow)
          label->setShadow(colorSpecFromRole(ColorRole::Shadow, 0.5F), 0, 1);
        addChild(std::move(label));
      }
    }
  }

  bool SysmonGraphWidget::updateMetrics(const SysmonSnapshot& snapshot) {
    uiAssertNotRendering("SysmonGraphWidget::updateMetrics");
    bool changed = false;
    for (std::size_t i = 0; i < m_count; ++i) {
      const auto& reading = snapshot.reading(m_metrics[i]);
      if (m_readings[i] == reading)
        continue;
      changed = true;
      m_readings[i] = reading;
      if (m_labels[i] != nullptr)
        m_labels[i]->setText(
            formatSysmonValue(sysmonMetricDefinitions()[static_cast<std::size_t>(m_metrics[i])], reading)
        );
    }
    if (changed)
      m_plot->setSeries(
          m_readings[0].current ? m_readings[0].history : std::vector<float>{},
          m_count == 2 && m_readings[1].current ? m_readings[1].history : std::vector<float>{}
      );
    return changed;
  }

  void SysmonGraphWidget::layoutAtScale(Renderer& renderer, float scale) {
    const float gap = Style::spaceXs * scale;
    const float legendGap = Style::spaceMd * scale;
    std::array<float, 2> widths{};
    float headerHeight = 0;
    float headerWidth = 0;
    for (std::size_t i = 0; i < m_count; ++i) {
      auto& glyph = *m_glyphs[i];
      glyph.setGlyphSize(Style::baseGlyphSize * scale);
      glyph.measure(renderer);
      widths[i] = glyph.width();
      headerHeight = std::max(headerHeight, glyph.height());
      if (auto* label = m_labels[i]) {
        label->setFontSize(Style::fontSizeCaption * scale);
        label->measure(renderer);
        widths[i] += gap + label->width();
        headerHeight = std::max(headerHeight, label->height());
      }
      headerWidth += widths[i];
    }
    if (m_count == 2)
      headerWidth += legendGap;
    const float width = std::max(180.0F * scale, headerWidth);
    const float chartHeight = 80.0F * scale;
    const float headerY = chartHeight + Style::spaceSm * scale;
    m_plot->setSize(width, chartHeight);
    m_plot->sync(renderer, scale);
    float x = std::round((width - headerWidth) * 0.5F);
    for (std::size_t i = 0; i < m_count; ++i) {
      auto& glyph = *m_glyphs[i];
      glyph.setPosition(x, headerY + std::round((headerHeight - glyph.height()) * 0.5F));
      if (auto* label = m_labels[i])
        label->setPosition(x + glyph.width() + gap, headerY + std::round((headerHeight - label->height()) * 0.5F));
      x += widths[i] + legendGap;
    }
    setSize(width, headerY + headerHeight);
  }

} // namespace greeter
