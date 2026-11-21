#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <cstdint>
#include <string>

class Label;

namespace greeter {

  class DigitalClockWidget final : public WidgetContent {
  public:
    struct Options {
      std::string format;
      std::string timezone;
      std::string fontFamily;
      ColorSpec color;
      bool shadow;
      bool centerText;
    };

    explicit DigitalClockWidget(Options options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool alignsToStart() const noexcept override { return !m_centerText; }
    std::optional<std::chrono::seconds> tickInterval() const override;
    bool updateTime(std::chrono::sys_seconds now) override;

  private:
    Label* m_label = nullptr;
    std::string m_format;
    std::string m_timezone;
    bool m_shadow;
    bool m_centerText;
    std::string m_sample;
    float m_stableWidth = 0.0F;
    float m_metricsFontSize = -1.0F;
    float m_metricsRenderScale = -1.0F;
    std::uint64_t m_metricsGeneration = 0;
    char m_widestDigit = '0';
  };

} // namespace greeter
