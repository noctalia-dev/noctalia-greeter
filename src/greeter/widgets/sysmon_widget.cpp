#include "greeter/widgets/sysmon_widget.h"

#include "core/ui_phase.h"

#include <memory>
#include <sysmon_widget.h>

namespace greeter {

  SysmonWidget::SysmonWidget(const Options& options)
      : m_metrics{options.primary, options.secondary.value_or(options.primary)},
        m_count(!options.gauge && options.secondary ? 2 : 1), m_gauge(options.gauge), m_color(options.color),
        m_highlightColor(options.highlightColor) {
    setHitTestVisible(false);
    const auto glyph = [](SysmonMetric metric) {
      return std::string(sysmonMetricDefinitions()[static_cast<std::size_t>(metric)].glyph);
    };
    auto widget = std::make_unique<::DesktopSysmon>(::DesktopSysmon::Options{
        .glyph = glyph(m_metrics[0]),
        .glyph2 = m_count == 2 ? glyph(m_metrics[1]) : "",
        .displayMode = m_gauge ? DesktopSysmonDisplayMode::Gauge : DesktopSysmonDisplayMode::Graph,
        .gaugeLayout = options.stacked ? DesktopSysmonGaugeLayout::Vertical : DesktopSysmonGaugeLayout::Horizontal,
        .lineColor = options.color,
        .lineColor2 = options.color2,
        .showLabel = options.showLabel,
        .labelMinWidth = m_gauge ? options.labelMinWidth : 0.0F,
        .shadow = options.shadow,
    });
    m_widget = widget.get();
    widget->setFontFamily(options.fontFamily);
    widget->setText("--", "--");
    addChild(std::move(widget));
    if (m_gauge) {
      syncGauge();
      m_paletteConnection = paletteChanged().connect([this] { syncGauge(); });
    }
  }

  bool SysmonWidget::updateMetrics(const SysmonSnapshot& snapshot) {
    uiAssertNotRendering("SysmonWidget::updateMetrics");
    bool changed = false;
    for (std::size_t i = 0; i < m_count; ++i) {
      const auto& reading = snapshot.reading(m_metrics[i]);
      const auto& previous = m_readings[i];
      const bool unchanged = m_gauge ? previous.current == reading.current
              && previous.gauge == reading.gauge
              && previous.absence == reading.absence
                                     : previous == reading;
      if (unchanged)
        continue;
      m_readings[i] = reading;
      changed = true;
    }
    if (!changed)
      return false;
    m_widget->setText(
        formatSysmonValue(sysmonMetricDefinitions()[static_cast<std::size_t>(m_metrics[0])], m_readings[0]),
        m_count == 2
            ? formatSysmonValue(sysmonMetricDefinitions()[static_cast<std::size_t>(m_metrics[1])], m_readings[1])
            : ""
    );
    if (m_gauge)
      syncGauge();
    else
      m_historyDirty = true;
    return true;
  }

  void SysmonWidget::syncGauge() {
    const auto& reading = m_readings[0].gauge;
    m_widget->setGauge(
        reading ? reading->normalized : 0.0F,
        lerpHsvChromaWeighted(
            resolveColorSpec(m_color), resolveColorSpec(m_highlightColor), reading ? reading->highlight : 0.0F
        )
    );
  }

  void SysmonWidget::layoutAtScale(Renderer& renderer, float scale) {
    if (!m_gauge && m_historyDirty) {
      // Upload only from layout with a live renderer; snapshots remain provider-owned.
      m_widget->setHistory(
          renderer, m_readings[0].current ? m_readings[0].history : std::vector<float>{},
          m_count == 2 && m_readings[1].current ? m_readings[1].history : std::vector<float>{}
      );
      m_widget->setScroll(0.0F);
      m_historyDirty = false;
    }
    m_widget->setContentScale(scale);
    m_widget->layout(renderer);
    setSize(m_widget->width(), m_widget->height());
  }

} // namespace greeter
