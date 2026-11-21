#include "greeter/widgets/cpu_ram_provider.h"

#include "greeter/widgets/cpu_frequency_source.h"
#include "greeter/widgets/gpu_temperature_source.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace greeter {
  namespace {
    using CpuCounters = std::array<std::uint64_t, 8>;

    std::string_view token(std::string_view& input) {
      const auto start = input.find_first_not_of(" \t\r\n");
      if (start == std::string_view::npos) {
        input = {};
        return {};
      }
      input.remove_prefix(start);
      const auto end = input.find_first_of(" \t\r\n");
      const auto result = input.substr(0, end);
      input.remove_prefix(end == std::string_view::npos ? input.size() : end);
      return result;
    }

    std::optional<std::uint64_t> unsignedValue(std::string_view text) {
      if (text.empty())
        return std::nullopt;
      std::uint64_t value = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
      return value;
    }

    std::optional<CpuCounters> readCpu(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file)
        return std::nullopt;
      // Only the leading aggregate row is needed, regardless of the number of cores.
      std::string line;
      char character = 0;
      while (file.get(character) && character != '\n') {
        if (line.size() == 4096)
          return std::nullopt;
        line.push_back(character);
      }
      if (file.bad())
        return std::nullopt;
      std::string_view input = line;
      if (token(input) != "cpu")
        return std::nullopt;
      CpuCounters counters{};
      std::uint64_t total = 0;
      for (std::size_t i = 0; i < counters.size(); ++i) {
        const auto text = token(input);
        if (text.empty()) {
          if (i < 4)
            return std::nullopt;
          break;
        }
        const auto value = unsignedValue(text);
        if (!value || *value > std::numeric_limits<std::uint64_t>::max() - total)
          return std::nullopt;
        counters[i] = *value;
        total += *value;
      }
      // guest/guest_nice are already included in user/nice; never add them twice.
      return counters;
    }

    std::optional<double> cpuUsage(const CpuCounters& previous, const CpuCounters& current) {
      std::uint64_t totalDelta = 0;
      for (std::size_t i = 0; i < current.size(); ++i) {
        if (current[i] < previous[i])
          return std::nullopt;
        totalDelta += current[i] - previous[i];
      }
      if (totalDelta == 0)
        return std::nullopt;
      const auto idleDelta = (current[3] - previous[3]) + (current[4] - previous[4]);
      return 100.0 * (1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta));
    }

    struct MemoryValues {
      std::optional<double> ram;
      std::optional<double> swap;
      bool noSwap = false;
    };

    MemoryValues readMemory(const std::filesystem::path& path, CpuRamDemand demand) {
      std::ifstream file(path);
      if (!file)
        return {};
      std::array<char, 65537> buffer{};
      file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (file.bad() || file.gcount() == static_cast<std::streamsize>(buffer.size()))
        return {};
      std::string_view input(buffer.data(), static_cast<std::size_t>(file.gcount()));
      constexpr std::array<std::string_view, 4> keys{"MemTotal:", "MemAvailable:", "SwapTotal:", "SwapFree:"};
      std::array<std::optional<std::uint64_t>, 4> fields;
      std::array<bool, 2> valid{true, true};
      while (!input.empty()) {
        const auto end = input.find('\n');
        auto line = input.substr(0, end);
        input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
        const auto key = token(line);
        const auto* entry = std::ranges::find(keys, key);
        if (entry == keys.end())
          continue;
        const auto index = static_cast<std::size_t>(entry - keys.begin());
        if (index < 2 ? !demand.ram : !demand.swap)
          continue;
        auto& destination = fields[index];
        const auto value = unsignedValue(token(line));
        if (destination || !value || token(line) != "kB" || !token(line).empty()) {
          valid[index / 2] = false;
          continue;
        }
        destination = value;
      }
      MemoryValues result;
      if (demand.ram && valid[0] && fields[0] && *fields[0] > 0 && fields[1] && *fields[1] <= *fields[0])
        result.ram = 100.0 * (static_cast<double>(*fields[0] - *fields[1]) / static_cast<double>(*fields[0]));
      if (demand.swap && valid[1] && fields[2] && fields[3] && *fields[3] <= *fields[2]) {
        if (*fields[2] == 0)
          result.noSwap = true;
        else
          result.swap = 100.0 * (static_cast<double>(*fields[2] - *fields[3]) / static_cast<double>(*fields[2]));
      }
      return result;
    }

    float highlightFactor(double value, double activity, double critical) {
      // Desktop gradientFactor: no highlight below activity, 25% onset above it.
      if (value <= activity)
        return 0;
      if (value >= critical)
        return 1;
      return static_cast<float>(0.25 + 0.75 * (value - activity) / (critical - activity));
    }

    void record(SysmonReading& reading, std::optional<double> value, double activity, double critical) {
      if (!value) {
        reading = {};
        return;
      }
      reading.current = value;
      reading.absence = SysmonAbsence::Unavailable;
      const auto normalized = static_cast<float>(*value / 100.0);
      reading.gauge = SysmonGaugeReading{normalized, highlightFactor(*value, activity, critical)};
      if (reading.history.size() == 120)
        reading.history.erase(reading.history.begin());
      reading.history.push_back(normalized);
    }

    void recordFrequency(SysmonReading& reading, const CpuFrequencySample& sample) {
      if (!sample.currentMhz) {
        reading = {};
        return;
      }
      reading.current = sample.currentMhz;
      if (!sample.maximumMhz) {
        reading.gauge.reset();
        reading.history.clear();
        return;
      }
      const auto normalized = static_cast<float>(std::clamp(*sample.currentMhz / *sample.maximumMhz, 0.0, 1.0));
      reading.gauge = SysmonGaugeReading{normalized, highlightFactor(*sample.currentMhz / 1000.0, 2.5, 4.5)};
      if (reading.history.size() == 120)
        reading.history.erase(reading.history.begin());
      reading.history.push_back(normalized);
    }
  } // namespace

  CpuRamProvider::CpuRamProvider(
      std::filesystem::path procRoot, std::filesystem::path cpuRoot, std::filesystem::path hwmonRoot,
      std::filesystem::path thermalRoot
  )
      : m_procRoot(std::move(procRoot)), m_cpuRoot(std::move(cpuRoot)), m_hwmonRoot(std::move(hwmonRoot)),
        m_thermalRoot(std::move(thermalRoot)) {}

  void CpuRamProvider::recordTemperature(TemperatureState& state, const std::optional<TemperatureSample>& sample) {
    if (!sample) {
      state = {};
      return;
    }
    if (state.sensor != sample->sensor) {
      state = {};
      state.sensor = sample->sensor;
    }
    state.reading.current = sample->celsius;
    state.minimum = std::min(state.minimum, sample->celsius);
    state.maximum = std::max(state.maximum, sample->celsius);
    if (state.history.size() == 120)
      state.history.erase(state.history.begin());
    state.history.push_back(sample->celsius);
    const auto normalize = [&state](double value) {
      return static_cast<float>((value - state.minimum) / (state.maximum - state.minimum));
    };
    state.reading.history.clear();
    for (const auto value : state.history)
      state.reading.history.push_back(normalize(value));
    state.reading.gauge = SysmonGaugeReading{normalize(sample->celsius), highlightFactor(sample->celsius, 60, 85)};
  }

  void CpuRamProvider::setDemand(CpuRamDemand demand) {
    if (!demand.cpu) {
      m_previousCpu.reset();
      m_cpu = {};
    }
    if (!demand.ram)
      m_ram = {};
    if (!demand.swap)
      m_swap = {};
    if (!demand.frequency)
      m_frequency = {};
    if (!demand.temperature)
      m_temperature = {};
    if (!demand.gpuTemperature)
      m_gpuTemperature = {};
    m_demand = demand;
    publish();
  }

  const SysmonSnapshot& CpuRamProvider::sample() {
    if (m_demand.cpu) {
      const auto current = readCpu(m_procRoot / "stat");
      record(m_cpu, current && m_previousCpu ? cpuUsage(*m_previousCpu, *current) : std::nullopt, 50, 90);
      m_previousCpu = current;
    }
    if (m_demand.ram || m_demand.swap) {
      const auto memory = readMemory(m_procRoot / "meminfo", m_demand);
      if (m_demand.ram)
        record(m_ram, memory.ram, 60, 90);
      if (m_demand.swap) {
        record(m_swap, memory.swap, 20, 80);
        if (memory.noSwap)
          m_swap.absence = SysmonAbsence::NotConfigured;
      }
    }
    if (m_demand.frequency)
      recordFrequency(m_frequency, readCpuFrequency(m_cpuRoot));
    if (m_demand.temperature)
      recordTemperature(m_temperature, readCpuTemperature(m_hwmonRoot, m_thermalRoot));
    if (m_demand.gpuTemperature)
      recordTemperature(m_gpuTemperature, readGpuTemperature(m_hwmonRoot));
    publish();
    return m_snapshot;
  }

  void CpuRamProvider::publish() {
    auto snapshot = SysmonSnapshot::create(
        std::array{
            SysmonMetricReading{SysmonMetric::CpuUsage, m_cpu}, SysmonMetricReading{SysmonMetric::RamPct, m_ram},
            SysmonMetricReading{SysmonMetric::SwapPct, m_swap}, SysmonMetricReading{SysmonMetric::CpuFreq, m_frequency},
            SysmonMetricReading{SysmonMetric::CpuTemp, m_temperature.reading},
            SysmonMetricReading{SysmonMetric::GpuTemp, m_gpuTemperature.reading}
        }
    );
    if (!snapshot)
      throw std::logic_error("invalid CPU/RAM provider snapshot: " + snapshot.error());
    m_snapshot = std::move(*snapshot);
  }

} // namespace greeter
