#include "greeter/widgets/digital_clock_widget.h"

#include "time/time_format.h"

#include <digital_clock_widget.h>
#include <memory>
#include <utility>

namespace greeter {

  DigitalClockWidget::DigitalClockWidget(Options options)
      : m_format(std::move(options.format)), m_timezone(std::move(options.timezone)), m_centerText(options.centerText) {
    setHitTestVisible(false);
    auto clock = std::make_unique<::DigitalClockWidget>();
    m_clock = clock.get();
    clock->setFontFamily(std::move(options.fontFamily));
    clock->setColor(options.color);
    clock->setShadow(options.shadow);
    clock->setCenterText(m_centerText);
    addChild(std::move(clock));
    updateTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  }

  std::optional<std::chrono::seconds> DigitalClockWidget::tickInterval() const {
    if (m_format.empty())
      return std::nullopt;
    return std::chrono::seconds{formatShowsSeconds(m_format) ? 1 : 60};
  }

  bool DigitalClockWidget::updateTime(std::chrono::sys_seconds now) {
    return m_clock->setText(formatTimezoneUnixTime(now.time_since_epoch().count(), m_format, m_timezone));
  }

  void DigitalClockWidget::layoutAtScale(Renderer& renderer, float scale) {
    m_clock->setContentScale(scale);
    m_clock->layout(renderer);
    setSize(m_clock->width(), m_clock->height());
  }

} // namespace greeter
