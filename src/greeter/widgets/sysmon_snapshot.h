#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace greeter {

  enum class SysmonMetric : std::uint8_t {
    CpuUsage,
    CpuTemp,
    CpuFreq,
    GpuTemp,
    GpuUsage,
    GpuVram,
    GpuVramUsed,
    RamPct,
    SwapPct
  };
  enum class SysmonUnit : std::uint8_t { Percent, Celsius, Megahertz, Bytes };
  struct SysmonMetricDefinition {
    std::string_view name;
    SysmonMetric metric;
    std::string_view glyph;
    SysmonUnit unit;
  };
  std::span<const SysmonMetricDefinition> sysmonMetricDefinitions();
  const SysmonMetricDefinition* findSysmonMetric(std::string_view name);

  struct SysmonReading {
    // Raw value in the metric's documented unit; null is unavailable, not zero.
    std::optional<double> current;
    // Oldest first, normalized by the future provider; at most 120 finite [0..1] samples.
    std::vector<float> history;
    bool operator==(const SysmonReading&) const = default;
  };
  struct SysmonMetricReading {
    SysmonMetric metric;
    SysmonReading reading;
  };
  class SysmonSnapshot {
  public:
    static std::expected<SysmonSnapshot, std::string> create(std::span<const SysmonMetricReading> readings);
    const SysmonReading& reading(SysmonMetric metric) const;

  private:
    std::array<SysmonReading, 9> m_readings;
  };
  std::string formatSysmonValue(const SysmonMetricDefinition& metric, std::optional<double> value);

} // namespace greeter
