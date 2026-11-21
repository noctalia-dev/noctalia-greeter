#pragma once

#include <filesystem>
#include <optional>

namespace greeter {

  struct CpuFrequencySample {
    std::optional<double> currentMhz;
    std::optional<double> maximumMhz;
  };

  // Average readable nonzero per-core scaling_cur_freq, and highest nonzero
  // scaling_max_freq. The root is an internal fixture seam, not configuration.
  CpuFrequencySample readCpuFrequency(const std::filesystem::path& cpuRoot);

} // namespace greeter
