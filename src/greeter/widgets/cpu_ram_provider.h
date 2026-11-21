#pragma once

#include "greeter/widgets/cpu_temperature_source.h"
#include "greeter/widgets/sysmon_snapshot.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace greeter {

  struct CpuRamDemand {
    bool cpu = false;
    bool ram = false;
    bool swap = false;
    bool frequency = false;
    bool temperature = false;
  };

  // One owner shares sampled snapshots across views. No timer, thread, callback or
  // persistent file handle: the owner samples at its chosen cadence outside Render.
  class CpuRamProvider {
  public:
    // An internal source seam for fixtures, not a user-facing configuration path.
    explicit CpuRamProvider(
        std::filesystem::path procRoot = "/proc", std::filesystem::path cpuRoot = "/sys/devices/system/cpu",
        std::filesystem::path hwmonRoot = "/sys/class/hwmon", std::filesystem::path thermalRoot = "/sys/class/thermal"
    );
    void setDemand(CpuRamDemand demand);
    const SysmonSnapshot& sample();
    const SysmonSnapshot& snapshot() const noexcept { return m_snapshot; }

  private:
    void publish();
    void resetTemperature();
    void sampleTemperature();

    std::filesystem::path m_procRoot;
    std::filesystem::path m_cpuRoot;
    std::filesystem::path m_hwmonRoot;
    std::filesystem::path m_thermalRoot;
    CpuRamDemand m_demand;
    std::optional<std::array<std::uint64_t, 8>> m_previousCpu;
    SysmonReading m_cpu;
    SysmonReading m_ram;
    SysmonReading m_swap;
    SysmonReading m_frequency;
    SysmonReading m_temperature;
    std::optional<CpuTemperatureSensor> m_temperatureSensor;
    std::vector<double> m_temperatureHistory;
    double m_temperatureMin = 30;
    double m_temperatureMax = 80;
    SysmonSnapshot m_snapshot;
  };

} // namespace greeter
