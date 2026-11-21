#include "greeter/widgets/sysmon_snapshot.h"
#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  void rejects(greeter::SysmonMetric metric, greeter::SysmonReading reading) {
    require(
        !greeter::SysmonSnapshot::create(std::array{greeter::SysmonMetricReading{metric, std::move(reading)}}),
        "invalid metric snapshot accepted"
    );
  }
} // namespace

int main() {
  try {
    using namespace greeter;
    const auto fields = widgetSettingDefinitions("sysmon");
    const auto stat = std::ranges::find(*fields, "stat", &WidgetSettingDefinition::key);
    require(
        stat != fields->end() && stat->enumValues.size() == sysmonMetricDefinitions().size(),
        "metric catalog/schema mismatch"
    );
    for (const auto name : stat->enumValues)
      require(findSysmonMetric(name) != nullptr, "supported metric has no presentation definition");
    for (const auto name :
         std::array<std::string_view, 6>{"net_rx", "net_tx", "cpu", "CPU_USAGE", " cpu_usage", "cpu_usage "})
      require(findSysmonMetric(name) == nullptr, "unsupported metric acquired an alias");
    SysmonSnapshot empty;
    require(!empty.reading(SysmonMetric::CpuUsage).current, "missing reading fabricated zero");
    auto snapshot =
        SysmonSnapshot::create(std::array{SysmonMetricReading{SysmonMetric::CpuUsage, {0.0, {0, 0.5F, 1}}}});
    require(
        snapshot.has_value() && snapshot->reading(SysmonMetric::CpuUsage).current == 0.0, "real zero not preserved"
    );
    require(!snapshot->reading(SysmonMetric::GpuTemp).current, "missing GPU reading fabricated");
    const auto duplicate = SysmonMetricReading{SysmonMetric::CpuUsage, {}};
    require(!SysmonSnapshot::create(std::array{duplicate, duplicate}), "duplicate metric accepted");
    rejects(static_cast<SysmonMetric>(255), {});
    for (const auto value :
         {-1.0, 100.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
      rejects(SysmonMetric::CpuUsage, {value, {}});
    rejects(SysmonMetric::CpuTemp, {-273.16, {}});
    rejects(SysmonMetric::CpuFreq, {-1, {}});
    rejects(SysmonMetric::GpuVramUsed, {-1, {}});
    for (const float value :
         {-0.1F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
      rejects(SysmonMetric::CpuUsage, {50, {value}});
    rejects(SysmonMetric::CpuUsage, {50, std::vector<float>(121, 0.5F)});
    require(
        SysmonSnapshot::create(
            std::array{SysmonMetricReading{SysmonMetric::CpuUsage, {50, std::vector<float>(120, 0.5F)}}}
        ).has_value(),
        "bounded history rejected"
    );
    for (const auto& metric : sysmonMetricDefinitions())
      require(formatSysmonValue(metric, std::nullopt) == "--", "unavailable value formatting changed");
    require(formatSysmonValue(*findSysmonMetric("cpu_usage"), 42) == "42%", "percent unit incorrect");
    require(formatSysmonValue(*findSysmonMetric("cpu_temp"), -10) == "-10°C", "temperature unit incorrect");
    require(formatSysmonValue(*findSysmonMetric("cpu_freq"), 2400) == "2.4 GHz", "frequency unit incorrect");
    require(
        formatSysmonValue(*findSysmonMetric("gpu_vram_used"), 3.0 * 1024 * 1024 * 1024) == "3.0 GiB",
        "memory unit incorrect"
    );
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "sysmon-snapshot-test: {}", error.what());
    return 1;
  }
}
