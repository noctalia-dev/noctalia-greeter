#include "greeter/widgets/clock_widget.h"

#include "time/time_format.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <utility>

namespace greeter {
  namespace {
    std::chrono::seconds timeOfDay(std::chrono::sys_seconds now, const std::string& timezoneName) {
      using namespace std::chrono;
      if (timezoneName.empty()) {
        const std::time_t timestamp = system_clock::to_time_t(now);
        std::tm local{};
        localtime_r(&timestamp, &local);
        return hours{local.tm_hour} + minutes{local.tm_min} + seconds{local.tm_sec};
      }
      const auto local = locate_zone(timezoneName)->to_local(now);
      return local - floor<days>(local);
    }
  } // namespace

  ClockWidget::ClockWidget(Options options)
      : m_format(std::move(options.format)), m_timezone(std::move(options.timezone)) {
    setHitTestVisible(false);
    auto clock = std::make_unique<::DesktopClock>();
    m_clock = clock.get();
    clock->setStyle(options.style);
    clock->setFontFamily(std::move(options.fontFamily));
    clock->setColor(options.color);
    clock->setFontSize(options.fontSize);
    clock->setFontWeight(options.fontWeight);
    clock->setTightText(options.tightText);
    clock->setShadow(options.shadow);
    clock->setCenterText(options.centerText);
    clock->setShowCircle(options.showCircle);
    clock->setShowSeconds(options.showSeconds);
    addChild(std::move(clock));
    updateTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  }

  bool ClockWidget::alignsToStart() const noexcept {
    return m_clock->style() == ::DesktopClock::Style::Digital && !m_clock->centerText();
  }

  std::optional<std::chrono::seconds> ClockWidget::tickInterval() const {
    if (m_clock->style() == ::DesktopClock::Style::Analog)
      return std::chrono::seconds{m_clock->showSeconds() ? 1 : 60};
    if (m_format.empty())
      return std::nullopt;
    return std::chrono::seconds{formatShowsSeconds(m_format) ? 1 : 60};
  }

  bool ClockWidget::updateTime(std::chrono::sys_seconds now) {
    if (m_clock->style() == ::DesktopClock::Style::Analog)
      return m_clock->setTime(timeOfDay(now, m_timezone));
    return m_clock->setText(formatTimezoneUnixTime(now.time_since_epoch().count(), m_format, m_timezone));
  }

  NodeInvalidation ClockWidget::timeInvalidation() const noexcept {
    return m_clock->style() == ::DesktopClock::Style::Analog ? NodeInvalidation::Paint : NodeInvalidation::Layout;
  }

  void ClockWidget::layoutAtScale(Renderer& renderer, float scale) {
    m_clock->setContentScale(scale);
    m_clock->layout(renderer);
    setSize(m_clock->width(), m_clock->height());
  }

} // namespace greeter
