#include "greeter/widgets/gpu_temperature_source.h"

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
      int score;
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

    std::string lower(std::string value) {
      for (auto& character : value)
        if (character >= 'A' && character <= 'Z')
          character = static_cast<char>(character - 'A' + 'a');
      return value;
    }

    int score(std::string_view name, const std::string& label) {
      const int driver = name == "nouveau"                                         ? 10
          : name == "amdgpu" || name == "nvidia" || name == "i915" || name == "xe" ? 20
                                                                                   : -1;
      if (driver < 0)
        return -1;
      const auto normalized = lower(label);
      if (normalized.contains("junction") || normalized.contains("edge"))
        return driver + 30;
      if (normalized.contains("gpu") || normalized.contains("mem"))
        return driver + 25;
      return driver;
    }

    bool awake(const std::filesystem::path& hwmon) {
      const auto status = readText(hwmon / "device/power/runtime_status");
      return status && (*status == "active" || *status == "unsupported");
    }
  } // namespace

  std::optional<TemperatureSample> readGpuTemperature(const std::filesystem::path& hwmonRoot) {
    std::size_t visited = 0;
    const auto hwmons = entries(hwmonRoot, visited);
    if (!hwmons)
      return std::nullopt;
    std::vector<Candidate> candidates;
    for (const auto& hwmon : *hwmons) {
      if (!indexOf(hwmon.filename().string(), "hwmon"))
        continue;
      const auto name = readText(hwmon / "name");
      if (!name || score(*name, {}) < 0 || !awake(hwmon))
        continue;
      const auto files = entries(hwmon, visited);
      if (!files)
        return std::nullopt;
      for (const auto& input : *files) {
        const auto filename = input.filename().string();
        const auto index = indexOf(filename, "temp", "_input");
        if (!index || *index == 0)
          continue;
        // Recheck after discovery, before attributes that may call into the GPU driver.
        if (!awake(hwmon))
          break;
        const auto base = filename.substr(0, filename.size() - 6);
        if (!optionalFlag(hwmon / (base + "_fault"), "0") || !optionalFlag(hwmon / (base + "_enable"), "1"))
          continue;
        const auto value = readCelsius(input);
        if (!value)
          continue;
        const auto label = readText(hwmon / (base + "_label")).value_or("");
        candidates.push_back({{*value, {input, *name, label}}, score(*name, label)});
      }
    }
    if (candidates.empty())
      return std::nullopt;
    const auto rank = [](const Candidate& candidate) {
      return std::tuple{-candidate.score, -candidate.sample.celsius, candidate.sample.sensor.input};
    };
    auto best = std::ranges::min_element(candidates, {}, rank);
    return std::move(best->sample);
  }

} // namespace greeter
