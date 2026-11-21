#include "greeter/widgets/cpu_temperature_source.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace greeter {
  namespace {
    struct Candidate {
      TemperatureSample sample;
      int driverPriority;
      int sensorPriority;
    };

    std::optional<std::string> readText(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file)
        return std::nullopt;
      std::array<char, 257> buffer{};
      file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (file.bad() || file.gcount() == static_cast<std::streamsize>(buffer.size()))
        return std::nullopt;
      std::string_view text(buffer.data(), static_cast<std::size_t>(file.gcount()));
      const auto start = text.find_first_not_of(" \t\r\n");
      if (start == std::string_view::npos)
        return std::nullopt;
      return std::string(text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1));
    }

    std::optional<double> readCelsius(const std::filesystem::path& path) {
      const auto text = readText(path);
      if (!text)
        return std::nullopt;
      std::int64_t raw = 0;
      const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), raw);
      if (error != std::errc{} || end != text->data() + text->size() || raw < -273150)
        return std::nullopt;
      return static_cast<double>(raw) / 1000.0;
    }

    bool optionalFlag(const std::filesystem::path& path, std::string_view expected) {
      std::error_code error;
      const bool present = std::filesystem::exists(path, error);
      if (error)
        return false;
      if (!present)
        return true;
      const auto value = readText(path);
      return value && *value == expected;
    }

    std::optional<unsigned> indexOf(std::string_view name, std::string_view prefix, std::string_view suffix = {}) {
      if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() <= prefix.size() + suffix.size())
        return std::nullopt;
      name = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
      unsigned index = 0;
      const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), index);
      if (error != std::errc{} || end != name.data() + name.size())
        return std::nullopt;
      return index;
    }

    std::optional<std::vector<std::filesystem::path>> entries(const std::filesystem::path& root, std::size_t& visited) {
      std::vector<std::filesystem::path> paths;
      std::error_code error;
      std::filesystem::directory_iterator entry(root, error);
      if (error)
        return paths;
      for (; entry != std::filesystem::directory_iterator{}; entry.increment(error)) {
        if (error)
          return std::vector<std::filesystem::path>{};
        if (++visited > 4096)
          return std::nullopt;
        paths.push_back(entry->path());
      }
      if (error)
        paths.clear();
      return paths;
    }

    template <std::size_t N> int priority(std::string_view name, const std::array<std::string_view, N>& names) {
      const auto* found = std::ranges::find(names, name);
      return found == names.end() ? -1 : static_cast<int>(found - names.begin());
    }

    int sensorPriority(std::string_view driver, std::string_view label, unsigned index) {
      if (driver == "k10temp" || driver == "zenpower") {
        if (label.starts_with("Tctl"))
          return 0;
        if (index == 1)
          return 1;
        if (label.starts_with("Tdie"))
          return 2;
        if (label.starts_with("Package id"))
          return 3;
        if (label.starts_with("SoC Temperature"))
          return 4;
        if (label.starts_with("Core") || label.starts_with("Tccd"))
          return 5;
      } else if (driver == "coretemp") {
        if (label.starts_with("Package id"))
          return 0;
        if (index == 1)
          return 1;
        if (label.starts_with("Core"))
          return 2;
      } else if (driver == "ibmpowernv") {
        std::string lower(label);
        for (auto& character : lower)
          if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
        if (lower.contains("core"))
          return 0;
        if (index == 1)
          return 1;
      }
      return 20;
    }

    std::optional<TemperatureSample> choose(std::vector<Candidate>& candidates) {
      if (candidates.empty())
        return std::nullopt;
      const auto rank = [](const Candidate& candidate) {
        return std::tuple{
            candidate.sample.celsius == 0, candidate.driverPriority, candidate.sensorPriority,
            candidate.sample.sensor.input
        };
      };
      auto best = std::ranges::min_element(candidates, {}, rank);
      return std::move(best->sample);
    }
  } // namespace

  std::optional<TemperatureSample>
  readCpuTemperature(const std::filesystem::path& hwmonRoot, const std::filesystem::path& thermalRoot) {
    std::size_t visited = 0;
    const auto hwmons = entries(hwmonRoot, visited);
    if (!hwmons)
      return std::nullopt;
    std::vector<Candidate> candidates;
    constexpr std::array<std::string_view, 4> drivers{"k10temp", "zenpower", "coretemp", "ibmpowernv"};
    for (const auto& hwmon : *hwmons) {
      if (!indexOf(hwmon.filename().string(), "hwmon"))
        continue;
      const auto name = readText(hwmon / "name");
      const int driver = name ? priority(*name, drivers) : -1;
      if (driver < 0)
        continue;
      const auto files = entries(hwmon, visited);
      if (!files)
        return std::nullopt;
      for (const auto& input : *files) {
        const auto index = indexOf(input.filename().string(), "temp", "_input");
        if (!index || *index == 0)
          continue;
        const auto base = input.filename().string().substr(0, input.filename().string().size() - 6);
        if (!optionalFlag(hwmon / (base + "_fault"), "0") || !optionalFlag(hwmon / (base + "_enable"), "1"))
          continue;
        const auto value = readCelsius(input);
        if (!value)
          continue;
        const auto label = readText(hwmon / (base + "_label")).value_or("");
        candidates.push_back({{*value, {input, *name, label}}, driver, sensorPriority(*name, label, *index)});
      }
    }
    if (!candidates.empty())
      return choose(candidates);
    const auto zones = entries(thermalRoot, visited);
    if (!zones)
      return std::nullopt;
    constexpr std::array<std::string_view, 3> types{"cpu-thermal", "x86_pkg_temp", "acpitz"};
    for (const auto& zone : *zones) {
      if (!indexOf(zone.filename().string(), "thermal_zone"))
        continue;
      const auto type = readText(zone / "type");
      const int order = type ? priority(*type, types) : -1;
      if (order < 0)
        continue;
      if (const auto value = readCelsius(zone / "temp"))
        candidates.push_back({{*value, {zone / "temp", *type, {}}}, order, 0});
    }
    return choose(candidates);
  }

} // namespace greeter
