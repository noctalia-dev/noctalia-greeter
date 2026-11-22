#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <clock_widget.h>
#include <string>

namespace greeter {

  class ClockWidget final : public WidgetContent {
  public:
    struct Options {
      ::DesktopClock::Style style;
      std::string format;
      std::string timezone;
      std::string fontFamily;
      ColorSpec color;
      bool shadow;
      bool centerText;
      bool showCircle;
      bool showSeconds;
      float fontSize = 0;
      FontWeight fontWeight = FontWeight::Bold;
      bool tightText = false;
    };

    explicit ClockWidget(Options options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool alignsToStart() const noexcept override;
    std::optional<std::chrono::seconds> tickInterval() const override;
    bool updateTime(std::chrono::sys_seconds now) override;
    NodeInvalidation timeInvalidation() const noexcept override;

  private:
    ::DesktopClock* m_clock = nullptr;
    std::string m_format;
    std::string m_timezone;
  };

} // namespace greeter
