#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <array>
#include <string>

class Box;

namespace greeter {

  class AnalogClockWidget final : public WidgetContent {
  public:
    struct Options {
      std::string timezone;
      ColorSpec color;
      bool showCircle;
      bool showSeconds;
    };

    explicit AnalogClockWidget(Options options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    std::optional<std::chrono::seconds> tickInterval() const override;
    bool updateTime(std::chrono::sys_seconds now) override;
    NodeInvalidation timeInvalidation() const noexcept override { return NodeInvalidation::Paint; }

  private:
    std::string m_timezone;
    ColorSpec m_color;
    bool m_showSeconds;
    std::optional<std::chrono::seconds> m_lastTime;
    Box* m_face = nullptr;
    std::array<Node*, 60> m_ticks{};
    Node* m_hour = nullptr;
    Node* m_minute = nullptr;
    Node* m_second = nullptr;
    Box* m_hub = nullptr;
  };

} // namespace greeter
