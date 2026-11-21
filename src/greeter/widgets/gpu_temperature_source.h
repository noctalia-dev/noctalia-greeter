#pragma once

#include "greeter/widgets/temperature_sample.h"

#include <optional>

namespace greeter {

  // Known GPU hwmon drivers whose device reports active/unsupported runtime PM.
  // No vendor SDK, device handle or power-control write. The root is a fixture seam.
  std::optional<TemperatureSample> readGpuTemperature(const std::filesystem::path& hwmonRoot);

} // namespace greeter
