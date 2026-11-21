#include "greeter/widgets/cpu_frequency_source.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>

namespace greeter {
  namespace {
    bool isCpu(std::string_view name) {
      if (!name.starts_with("cpu") || name.size() <= 3)
        return false;
      name.remove_prefix(3);
      return std::ranges::all_of(name, [](char digit) { return digit >= '0' && digit <= '9'; });
    }

    std::optional<std::uint64_t> readKhz(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file)
        return std::nullopt;
      std::array<char, 65> buffer{};
      file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (file.bad() || file.gcount() == static_cast<std::streamsize>(buffer.size()))
        return std::nullopt;
      std::string_view text(buffer.data(), static_cast<std::size_t>(file.gcount()));
      const auto first = text.find_first_not_of(" \t\r\n");
      if (first == std::string_view::npos)
        return std::nullopt;
      text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
      std::uint64_t khz = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), khz);
      if (error != std::errc{} || end != text.data() + text.size() || khz == 0)
        return std::nullopt;
      return khz;
    }
  } // namespace

  CpuFrequencySample readCpuFrequency(const std::filesystem::path& cpuRoot) {
    std::error_code error;
    std::filesystem::directory_iterator entry(cpuRoot, error);
    if (error)
      return {};
    double totalMhz = 0;
    std::size_t count = 0;
    std::size_t visited = 0;
    std::uint64_t maximumKhz = 0;
    for (; entry != std::filesystem::directory_iterator{}; entry.increment(error)) {
      if (error || ++visited > 16384)
        return {};
      const auto& path = entry->path();
      std::error_code typeError;
      if (!isCpu(path.filename().string()) || !entry->is_directory(typeError))
        continue;
      const auto base = path / "cpufreq";
      if (const auto current = readKhz(base / "scaling_cur_freq")) {
        totalMhz += static_cast<double>(*current) / 1000.0;
        ++count;
      }
      if (const auto maximum = readKhz(base / "scaling_max_freq"))
        maximumKhz = std::max(maximumKhz, *maximum);
    }
    if (error)
      return {};
    CpuFrequencySample sample;
    if (count > 0)
      sample.currentMhz = totalMhz / static_cast<double>(count);
    if (maximumKhz > 0)
      sample.maximumMhz = static_cast<double>(maximumKhz) / 1000.0;
    return sample;
  }

} // namespace greeter
