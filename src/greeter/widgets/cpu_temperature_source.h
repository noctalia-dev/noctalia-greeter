#pragma once

#include "greeter/widgets/temperature_sample.h"

#include <optional>

namespace greeter {

  // Known CPU hwmon drivers, then known thermal zone types. Roots are fixture seams.
  std::optional<TemperatureSample>
  readCpuTemperature(const std::filesystem::path& hwmonRoot, const std::filesystem::path& thermalRoot);

} // namespace greeter
