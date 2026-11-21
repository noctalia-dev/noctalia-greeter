#include "greeter/widgets/analog_clock_widget.h"

#include "ui/controls/box.h"
#include "ui/style.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <numbers>
#include <utility>

namespace greeter {
  namespace {
    constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0F;

    Box* addShape(Node& parent, const ColorSpec& color) {
      auto shape = std::make_unique<Box>();
      shape->setHitTestVisible(false);
      shape->setFill(color);
      auto* result = shape.get();
      parent.addChild(std::move(shape));
      return result;
    }

    // Zero-size pivots use the same origin in rendering and scene coordinate mapping.
    Node* addPivot(Node& parent, const ColorSpec& color) {
      auto pivot = std::make_unique<Node>();
      pivot->setHitTestVisible(false);
      addShape(*pivot, color);
      auto* result = pivot.get();
      parent.addChild(std::move(pivot));
      return result;
    }

    Box& pivotShape(Node& pivot) { return *static_cast<Box*>(pivot.children().front().get()); }

    void layoutHand(Node& pivot, float center, float width, float length) {
      pivot.setPosition(center, center);
      auto& hand = pivotShape(pivot);
      hand.setSize(width, length);
      hand.setPosition(-width * 0.5F, -length);
      hand.setRadius(width * 0.5F);
    }

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

  AnalogClockWidget::AnalogClockWidget(Options options)
      : m_timezone(std::move(options.timezone)), m_color(options.color), m_showSeconds(options.showSeconds) {
    setHitTestVisible(false);
    m_face = addShape(*this, clearColorSpec());
    m_face->setVisible(options.showCircle);
    for (auto*& tick : m_ticks) {
      tick = addPivot(*this, m_color);
      tick->setVisible(options.showCircle);
    }
    m_hour = addPivot(*this, m_color);
    m_minute = addPivot(*this, m_color);
    m_second = addPivot(*this, scaleAlpha(m_color, 0.72F));
    m_second->setVisible(m_showSeconds);
    m_hub = addShape(*this, m_color);
    updateTime(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  }

  std::optional<std::chrono::seconds> AnalogClockWidget::tickInterval() const {
    return std::chrono::seconds{m_showSeconds ? 1 : 60};
  }

  bool AnalogClockWidget::updateTime(std::chrono::sys_seconds now) {
    auto local = timeOfDay(now, m_timezone);
    if (!m_showSeconds)
      local = std::chrono::floor<std::chrono::minutes>(local);
    if (m_lastTime == local)
      return false;
    m_lastTime = local;
    const std::chrono::hh_mm_ss time{local};
    const float hour = static_cast<float>(time.hours().count() % 12);
    const float minute = static_cast<float>(time.minutes().count());
    const float second = static_cast<float>(time.seconds().count());
    m_hour->setRotation((hour + minute / 60.0F + second / 3600.0F) * 30.0F * kRadiansPerDegree);
    m_minute->setRotation((minute + second / 60.0F) * 6.0F * kRadiansPerDegree);
    m_second->setRotation(second * 6.0F * kRadiansPerDegree);
    return true;
  }

  void AnalogClockWidget::layoutAtScale(Renderer& /*renderer*/, float scale) {
    const float size = Style::fontSizeBody * 5.25F * scale;
    const float center = size * 0.5F;
    const float borderWidth = std::max(1.5F, 2.0F * scale);
    const float hourLength = std::max(4.0F, 5.5F * scale);
    const float hourWidth = std::max(1.75F, 2.25F * scale);
    const float padding = std::max(3.0F, 3.5F * scale) + hourWidth * 0.5F;
    const float radius = std::max(1.0F, center - padding - borderWidth * 0.5F);
    m_face->setPosition(center - radius, center - radius);
    m_face->setSize(radius * 2.0F, radius * 2.0F);
    m_face->setBorder(m_color, borderWidth);
    m_face->setRadius(radius);
    for (std::size_t minute = 0; minute < m_ticks.size(); ++minute) {
      const bool hourMark = minute % 5 == 0;
      const float width = hourMark ? hourWidth : std::max(1.0F, 1.25F * scale);
      const float length = hourMark ? hourLength : std::max(2.5F, 3.0F * scale);
      auto& pivot = *m_ticks[minute];
      pivot.setPosition(center, center);
      pivot.setRotation(static_cast<float>(minute) * 6.0F * kRadiansPerDegree);
      auto& tick = pivotShape(pivot);
      tick.setPosition(-width * 0.5F, -radius);
      tick.setSize(width, length);
      tick.setRadius(width * 0.5F);
    }
    layoutHand(*m_hour, center, std::max(2.2F, 2.65F * scale), radius * 0.54F);
    layoutHand(*m_minute, center, std::max(1.75F, 2.0F * scale), radius * 0.90F);
    layoutHand(*m_second, center, std::max(1.0F, 1.25F * scale), radius * 0.84F);
    const float hubSize = std::max(4.0F, 5.0F * scale);
    m_hub->setSize(hubSize, hubSize);
    m_hub->setPosition(center - hubSize * 0.5F, center - hubSize * 0.5F);
    m_hub->setRadius(hubSize * 0.5F);
    setSize(size, size);
  }

} // namespace greeter
