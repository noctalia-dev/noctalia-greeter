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
    for (const auto name : std::array<std::string_view, 10>{
             "gpu_temp", "gpu_usage", "gpu_vram", "gpu_vram_used", "net_rx", "net_tx", "cpu", "CPU_USAGE", " cpu_usage",
             "cpu_usage "
         })
      require(findSysmonMetric(name) == nullptr, "unsupported metric acquired an alias");
    SysmonSnapshot empty;
    require(!empty.reading(SysmonMetric::CpuUsage).current, "missing reading fabricated zero");
    auto snapshot =
        SysmonSnapshot::create(std::array{SysmonMetricReading{SysmonMetric::CpuUsage, {0.0, {0, 0.5F, 1}}}});
    require(
        snapshot.has_value() && snapshot->reading(SysmonMetric::CpuUsage).current == 0.0, "real zero not preserved"
    );
    require(!snapshot->reading(SysmonMetric::RamPct).current, "missing reading fabricated");
    const auto duplicate = SysmonMetricReading{SysmonMetric::CpuUsage, {}};
    require(!SysmonSnapshot::create(std::array{duplicate, duplicate}), "duplicate metric accepted");
    rejects(static_cast<SysmonMetric>(255), {});
    for (const auto value :
         {-1.0, 100.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
      rejects(SysmonMetric::CpuUsage, {value, {}});
    rejects(SysmonMetric::CpuTemp, {-273.16, {}});
    rejects(SysmonMetric::CpuFreq, {-1, {}});
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
    for (const float value :
         {-0.1F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
      rejects(SysmonMetric::CpuUsage, {50, {}, SysmonGaugeReading{value, 0}});
      rejects(SysmonMetric::CpuUsage, {50, {}, SysmonGaugeReading{0.5F, value}});
    }
    rejects(SysmonMetric::CpuUsage, {std::nullopt, {}, SysmonGaugeReading{0.5F}});
    for (const float value : {0.0F, 1.0F}) {
      const auto gauge = SysmonSnapshot::create(
          std::array{SysmonMetricReading{SysmonMetric::CpuUsage, {50, {}, SysmonGaugeReading{value, value}}}}
      );
      require(
          gauge.has_value() && gauge->reading(SysmonMetric::CpuUsage).gauge->normalized == value,
          "gauge boundary not preserved"
      );
    }
    const SysmonReading noSwap{std::nullopt, {}, std::nullopt, SysmonAbsence::NotConfigured};
    require(
        SysmonSnapshot::create(std::array{SysmonMetricReading{SysmonMetric::SwapPct, noSwap}}).has_value(),
        "not-configured swap rejected"
    );
    require(
        formatSysmonValue(*findSysmonMetric("swap_pct"), noSwap) == "No swap",
        "not-configured swap formatting incorrect"
    );
    rejects(SysmonMetric::CpuUsage, noSwap);
    rejects(SysmonMetric::SwapPct, {0, {}, std::nullopt, SysmonAbsence::NotConfigured});
    rejects(SysmonMetric::SwapPct, {std::nullopt, {0}, std::nullopt, SysmonAbsence::NotConfigured});
    rejects(SysmonMetric::SwapPct, {std::nullopt, {}, SysmonGaugeReading{0}, SysmonAbsence::NotConfigured});
    rejects(SysmonMetric::SwapPct, {std::nullopt, {}, std::nullopt, static_cast<SysmonAbsence>(255)});
    for (const auto& metric : sysmonMetricDefinitions())
      require(formatSysmonValue(metric, {}) == "--", "unavailable value formatting changed");
    require(formatSysmonValue(*findSysmonMetric("cpu_usage"), {42, {}}) == "42%", "percent unit incorrect");
    require(formatSysmonValue(*findSysmonMetric("cpu_temp"), {-10, {}}) == "-10°C", "temperature unit incorrect");
    require(formatSysmonValue(*findSysmonMetric("cpu_freq"), {2400, {}}) == "2.4 GHz", "frequency unit incorrect");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "sysmon-snapshot-test: {}", error.what());
    return 1;
  }
}
