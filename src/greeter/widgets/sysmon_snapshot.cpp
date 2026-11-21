#include "greeter/widgets/sysmon_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>

namespace greeter {
  namespace {
    constexpr std::array<SysmonMetricDefinition, 9> kMetrics{{
        {"cpu_usage", SysmonMetric::CpuUsage, "cpu-usage", SysmonUnit::Percent},
        {"cpu_temp", SysmonMetric::CpuTemp, "cpu-temperature", SysmonUnit::Celsius},
        {"cpu_freq", SysmonMetric::CpuFreq, "performance", SysmonUnit::Megahertz},
        {"gpu_temp", SysmonMetric::GpuTemp, "temperature", SysmonUnit::Celsius},
        {"gpu_usage", SysmonMetric::GpuUsage, "gpu-usage", SysmonUnit::Percent},
        {"gpu_vram", SysmonMetric::GpuVram, "memory", SysmonUnit::Percent},
        {"gpu_vram_used", SysmonMetric::GpuVramUsed, "memory", SysmonUnit::Bytes},
        {"ram_pct", SysmonMetric::RamPct, "memory", SysmonUnit::Percent},
        {"swap_pct", SysmonMetric::SwapPct, "storage", SysmonUnit::Percent},
    }};
  } // namespace

  std::span<const SysmonMetricDefinition> sysmonMetricDefinitions() { return kMetrics; }

  const SysmonMetricDefinition* findSysmonMetric(std::string_view name) {
    const auto* const entry = std::ranges::find(kMetrics, name, &SysmonMetricDefinition::name);
    return entry == kMetrics.end() ? nullptr : entry;
  }

  std::expected<SysmonSnapshot, std::string> SysmonSnapshot::create(std::span<const SysmonMetricReading> readings) {
    if (readings.size() > kMetrics.size())
      return std::unexpected("too many local metrics");
    SysmonSnapshot snapshot;
    std::array<bool, kMetrics.size()> seen{};
    for (const auto& entry : readings) {
      const auto index = static_cast<std::size_t>(entry.metric);
      if (index >= kMetrics.size() || seen[index])
        return std::unexpected("unknown or duplicate local metric");
      seen[index] = true;
      const auto& data = entry.reading;
      if (data.absence != SysmonAbsence::Unavailable
          && (data.absence != SysmonAbsence::NotConfigured
              || entry.metric != SysmonMetric::SwapPct
              || data.current
              || data.gauge
              || !data.history.empty()))
        return std::unexpected("not-configured status requires swap with no value, gauge or history");
      if (data.gauge
          && (!data.current
              || !std::isfinite(data.gauge->normalized)
              || data.gauge->normalized < 0.0F
              || data.gauge->normalized > 1.0F
              || !std::isfinite(data.gauge->highlight)
              || data.gauge->highlight < 0.0F
              || data.gauge->highlight > 1.0F))
        return std::unexpected("gauge requires a current value and finite normalized fill/highlight in [0..1]");
      if (data.history.size() > 120 || std::ranges::any_of(data.history, [](float sample) {
            return !std::isfinite(sample) || sample < 0.0F || sample > 1.0F;
          }))
        return std::unexpected("expected at most 120 finite normalized history samples");
      if (data.current) {
        const auto unit = kMetrics[index].unit;
        if (!std::isfinite(*data.current)
            || (unit == SysmonUnit::Celsius ? *data.current < -273.15 : *data.current < 0)
            || (unit == SysmonUnit::Percent && *data.current > 100))
          return std::unexpected("invalid current metric value");
      }
      snapshot.m_readings[index] = data;
    }
    return snapshot;
  }

  const SysmonReading& SysmonSnapshot::reading(SysmonMetric metric) const {
    return m_readings.at(static_cast<std::size_t>(metric));
  }

  std::string formatSysmonValue(const SysmonMetricDefinition& metric, const SysmonReading& reading) {
    if (!reading.current)
      return reading.absence == SysmonAbsence::NotConfigured ? "No swap" : "--";
    const auto value = *reading.current;
    switch (metric.unit) {
    case SysmonUnit::Percent:
      return std::format("{:.0F}%", value);
    case SysmonUnit::Celsius:
      return std::format("{:.0F}°C", value);
    case SysmonUnit::Megahertz:
      return std::format("{:.1f} GHz", value / 1000.0);
    case SysmonUnit::Bytes:
      return std::format("{:.1F} GiB", value / (1024.0 * 1024.0 * 1024.0));
    }
    return "--";
  }

} // namespace greeter
