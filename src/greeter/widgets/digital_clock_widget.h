#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <string>

class DigitalClockWidget;

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
    ::DigitalClockWidget* m_clock = nullptr;
    std::string m_format;
    std::string m_timezone;
    bool m_centerText;
  };

} // namespace greeter
