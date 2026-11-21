#pragma once

#include "greeter/widgets/sysmon_snapshot.h"
#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <array>
#include <string>

class Glyph;
class Label;

namespace greeter {
  class SysmonGraphPlot;

  class SysmonGraphWidget final : public WidgetContent {
  public:
    struct Options {
      SysmonMetric primary;
      std::optional<SysmonMetric> secondary;
      ColorSpec color;
      ColorSpec color2;
      std::string fontFamily;
      bool showLabel;
      bool shadow;
    };
    explicit SysmonGraphWidget(const Options& options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool updateMetrics(const SysmonSnapshot& snapshot) override;

  private:
    std::array<SysmonMetric, 2> m_metrics;
    std::size_t m_count;
    std::array<SysmonReading, 2> m_readings;
    std::array<Glyph*, 2> m_glyphs{};
    std::array<Label*, 2> m_labels{};
    SysmonGraphPlot* m_plot = nullptr;
  };

} // namespace greeter
