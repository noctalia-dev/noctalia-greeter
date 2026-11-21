#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace greeter {

  struct CpuTemperatureSensor {
    std::filesystem::path input;
    std::string name;
    std::string label;
    bool operator==(const CpuTemperatureSensor&) const = default;
  };
  struct CpuTemperatureSample {
    double celsius;
    CpuTemperatureSensor sensor;
  };

  // Known CPU hwmon drivers, then known thermal zone types. Roots are fixture seams.
  std::optional<CpuTemperatureSample>
  readCpuTemperature(const std::filesystem::path& hwmonRoot, const std::filesystem::path& thermalRoot);

} // namespace greeter
