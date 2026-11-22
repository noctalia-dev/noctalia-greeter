#pragma once

#include "greeter/widgets/sysmon_snapshot.h"
#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <array>
#include <string>

class DesktopSysmon;

namespace greeter {

  class SysmonWidget final : public WidgetContent {
  public:
    struct Options {
      SysmonMetric primary;
      std::optional<SysmonMetric> secondary;
      bool gauge;
      ColorSpec color;
      ColorSpec color2;
      ColorSpec highlightColor;
      std::string fontFamily;
      bool stacked;
      bool showLabel;
      float labelMinWidth;
      bool shadow;
    };
    explicit SysmonWidget(const Options& options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool updateMetrics(const SysmonSnapshot& snapshot) override;

  private:
    void syncGauge();

    ::DesktopSysmon* m_widget = nullptr;
    std::array<SysmonMetric, 2> m_metrics;
    std::array<SysmonReading, 2> m_readings;
    std::size_t m_count;
    bool m_gauge;
    bool m_historyDirty = true;
    ColorSpec m_color;
    ColorSpec m_highlightColor;
    Signal<>::ScopedConnection m_paletteConnection;
  };

} // namespace greeter
