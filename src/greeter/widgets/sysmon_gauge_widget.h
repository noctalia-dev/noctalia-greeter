#pragma once

#include "greeter/widgets/sysmon_snapshot.h"
#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <string>

class Glyph;
class Label;
class ProgressBar;

namespace greeter {

  class SysmonGaugeWidget final : public WidgetContent {
  public:
    struct Options {
      SysmonMetric metric;
      ColorSpec color;
      ColorSpec highlightColor;
      std::string fontFamily;
      bool stacked;
      bool showLabel;
      float labelMinWidth;
      bool shadow;
    };
    explicit SysmonGaugeWidget(const Options& options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool updateMetrics(const SysmonSnapshot& snapshot) override;

  private:
    void syncColor();
    void syncProgress();

    SysmonMetric m_metric;
    ColorSpec m_color;
    ColorSpec m_highlightColor;
    bool m_stacked;
    float m_labelMinWidth;
    std::optional<double> m_current;
    std::optional<SysmonGaugeReading> m_reading;
    Glyph* m_glyph = nullptr;
    Label* m_label = nullptr;
    ProgressBar* m_gauge = nullptr;
    Signal<>::ScopedConnection m_paletteConnection;
  };

} // namespace greeter
