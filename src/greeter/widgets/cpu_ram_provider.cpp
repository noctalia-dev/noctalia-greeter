#include "greeter/widgets/cpu_ram_provider.h"

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

    std::optional<double> readRam(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file)
        return std::nullopt;
      std::array<char, 65537> buffer{};
      file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (file.bad() || file.gcount() == static_cast<std::streamsize>(buffer.size()))
        return std::nullopt;
      std::string_view input(buffer.data(), static_cast<std::size_t>(file.gcount()));
      std::optional<std::uint64_t> total;
      std::optional<std::uint64_t> available;
      while (!input.empty()) {
        const auto end = input.find('\n');
        auto line = input.substr(0, end);
        input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
        const auto key = token(line);
        if (key != "MemTotal:" && key != "MemAvailable:")
          continue;
        auto& destination = key == "MemTotal:" ? total : available;
        const auto value = unsignedValue(token(line));
        if (destination || !value || token(line) != "kB" || !token(line).empty())
          return std::nullopt;
        destination = value;
      }
      if (!total || *total == 0 || !available || *available > *total)
        return std::nullopt;
      return 100.0 * (static_cast<double>(*total - *available) / static_cast<double>(*total));
    }

    void record(SysmonReading& reading, std::optional<double> value, double activity, double critical) {
      if (!value) {
        reading = {};
        return;
      }
      reading.current = value;
      const auto normalized = static_cast<float>(*value / 100.0);
      // Desktop gradientFactor: no highlight below activity, 25% onset above it.
      const float highlight = *value <= activity ? 0.0F
          : *value >= critical                   ? 1.0F
                               : static_cast<float>(0.25 + 0.75 * (*value - activity) / (critical - activity));
      reading.gauge = SysmonGaugeReading{normalized, highlight};
      if (reading.history.size() == 120)
        reading.history.erase(reading.history.begin());
      reading.history.push_back(normalized);
    }
  } // namespace

  CpuRamProvider::CpuRamProvider(std::filesystem::path procRoot) : m_procRoot(std::move(procRoot)) {}

  void CpuRamProvider::setDemand(CpuRamDemand demand) {
    if (!demand.cpu) {
      m_previousCpu.reset();
      m_cpu = {};
    }
    if (!demand.ram)
      m_ram = {};
    m_demand = demand;
    publish();
  }

  const SysmonSnapshot& CpuRamProvider::sample() {
    if (m_demand.cpu) {
      const auto current = readCpu(m_procRoot / "stat");
      record(m_cpu, current && m_previousCpu ? cpuUsage(*m_previousCpu, *current) : std::nullopt, 50, 90);
      m_previousCpu = current;
    }
    if (m_demand.ram)
      record(m_ram, readRam(m_procRoot / "meminfo"), 60, 90);
    publish();
    return m_snapshot;
  }

  void CpuRamProvider::publish() {
    auto snapshot = SysmonSnapshot::create(
        std::array{SysmonMetricReading{SysmonMetric::CpuUsage, m_cpu}, SysmonMetricReading{SysmonMetric::RamPct, m_ram}}
    );
    if (!snapshot)
      throw std::logic_error("invalid CPU/RAM provider snapshot: " + snapshot.error());
    m_snapshot = std::move(*snapshot);
  }

} // namespace greeter
