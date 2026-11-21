#pragma once

#include <filesystem>
#include <string>

namespace greeter {

  struct TemperatureSensor {
    std::filesystem::path input;
    std::string name;
    std::string label;
    bool operator==(const TemperatureSensor&) const = default;
  };
  struct TemperatureSample {
    double celsius;
    TemperatureSensor sensor;
  };

} // namespace greeter
