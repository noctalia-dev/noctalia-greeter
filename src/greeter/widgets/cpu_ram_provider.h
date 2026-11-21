#pragma once

#include "greeter/widgets/sysmon_snapshot.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace greeter {

  struct CpuRamDemand {
    bool cpu = false;
    bool ram = false;
  };

  // One owner shares sampled snapshots across views. No timer, thread, callback or
  // persistent file handle: the owner samples at its chosen cadence outside Render.
  class CpuRamProvider {
  public:
    // An internal source seam for fixtures, not a user-facing configuration path.
    explicit CpuRamProvider(std::filesystem::path procRoot = "/proc");
    void setDemand(CpuRamDemand demand);
    const SysmonSnapshot& sample();
    const SysmonSnapshot& snapshot() const noexcept { return m_snapshot; }

  private:
    void publish();

    std::filesystem::path m_procRoot;
    CpuRamDemand m_demand;
    std::optional<std::array<std::uint64_t, 8>> m_previousCpu;
    SysmonReading m_cpu;
    SysmonReading m_ram;
    SysmonSnapshot m_snapshot;
  };

} // namespace greeter
