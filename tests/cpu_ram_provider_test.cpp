#include "greeter/widgets/cpu_ram_provider.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }

  struct Fixture {
    std::filesystem::path root;
    Fixture() {
      std::string name = "/tmp/noctalia-cpu-ram-test-XXXXXX";
      const auto* directory = ::mkdtemp(name.data());
      require(directory != nullptr, "cannot create fixture directory");
      root = directory;
    }
    ~Fixture() {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    }
    void write(std::string_view file, std::string_view text) const {
      std::ofstream output(root / file);
      output << text;
      require(output.good(), "fixture write failed");
    }
    void cpu(std::uint64_t busy, std::uint64_t idle) const {
      write("stat", std::format("cpu {} 0 0 {} 0 0 0 0 999 999\ncpu0 0 0 0 0\n", busy, idle));
    }
    void ram(std::uint64_t total, std::uint64_t available) const {
      write("meminfo", std::format("MemTotal: {} kB\nMemFree: 0 kB\nMemAvailable: {} kB\n", total, available));
    }
    void frequency(std::string_view core, std::string_view current, std::string_view maximum) const {
      const auto base = std::filesystem::path("cpus") / core / "cpufreq";
      std::filesystem::create_directories(root / base);
      write((base / "scaling_cur_freq").string(), current);
      write((base / "scaling_max_freq").string(), maximum);
    }
    void sensor(
        std::string_view directory, std::string_view driver, std::string_view label, std::string_view raw,
        unsigned index = 1
    ) const {
      const auto base = std::filesystem::path("hwmon") / directory;
      std::filesystem::create_directories(root / base);
      write((base / "name").string(), driver);
      write((base / std::format("temp{}_label", index)).string(), label);
      write((base / std::format("temp{}_input", index)).string(), raw);
    }
    void zone(std::string_view directory, std::string_view type, std::string_view raw) const {
      const auto base = std::filesystem::path("thermal") / directory;
      std::filesystem::create_directories(root / base);
      write((base / "type").string(), type);
      write((base / "temp").string(), raw);
    }
    void
    gpu(std::string_view directory, std::string_view driver, std::string_view label, std::string_view raw,
        std::string_view status = "active", unsigned index = 1) const {
      sensor(directory, driver, label, raw, index);
      const auto power = std::filesystem::path("hwmon") / directory / "device/power";
      std::filesystem::create_directories(root / power);
      write((power / "runtime_status").string(), status);
    }
  };

  void near(double value, double expected, const char* message) {
    require(std::abs(value - expected) < 0.0001, message);
  }
  void unavailable(const greeter::SysmonReading& reading) {
    require(
        !reading.current
            && !reading.gauge
            && reading.history.empty()
            && reading.absence == greeter::SysmonAbsence::Unavailable,
        "unavailable metric retained data"
    );
  }

  void checkSwap(const Fixture& fixture) {
    using namespace greeter;
    CpuRamProvider provider(fixture.root);
    provider.setDemand({false, false, true});
    fixture.write("meminfo", "SwapTotal: 1000 kB\nSwapFree: 500 kB\n");
    provider.sample();
    const auto retained = provider.snapshot();
    const auto& swap = retained.reading(SysmonMetric::SwapPct);
    near(*swap.current, 50, "swap-only demand required RAM fields");
    near(swap.gauge->normalized, 0.5, "swap fill normalization incorrect");
    near(swap.gauge->highlight, 0.625, "swap gradient differs from desktop");
    unavailable(retained.reading(SysmonMetric::RamPct));
    unavailable(retained.reading(SysmonMetric::CpuUsage));
    provider.setDemand({false, false, true});
    provider.sample();
    require(provider.snapshot().reading(SysmonMetric::SwapPct).history.size() == 2, "same swap demand reset history");

    for (const auto text : std::array<std::string_view, 12>{
             "", "SwapTotal: 1000 kB\n", "SwapFree: 100 kB\n", "SwapTotal: 0 kB\nSwapFree: 1 kB\n",
             "SwapTotal: 10 kB\nSwapFree: 11 kB\n", "SwapTotal: -1 kB\nSwapFree: 0 kB\n",
             "SwapTotal: 10 MB\nSwapFree: 0 kB\n", "SwapTotal: 10 kB\nSwapFree: 0 kB junk\n",
             "SwapTotal: 10 kB\nSwapTotal: 10 kB\nSwapFree: 0 kB\n",
             "SwapTotal: 18446744073709551616 kB\nSwapFree: 0 kB\n",
             "SwapTotal: 10 kB\nSwapFree: 1 kB\nSwapFree: 2 kB\n", "SwapTotal: 10 kB\nSwapFree: -1 kB\n"
         }) {
      fixture.write("meminfo", "MemTotal: 1000 kB\nMemAvailable: 400 kB\n" + std::string(text));
      provider.setDemand({false, true, true});
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::SwapPct));
      near(*provider.snapshot().reading(SysmonMetric::RamPct).current, 60, "bad swap fields poisoned RAM");
    }
    fixture.write("meminfo", "MemTotal: broken kB\nSwapTotal: 1000 kB\nSwapFree: 500 kB\n");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    near(*provider.snapshot().reading(SysmonMetric::SwapPct).current, 50, "bad RAM fields poisoned swap");

    fixture.write("meminfo", "SwapTotal: 0 kB\nSwapFree: 0 kB\n");
    provider.sample();
    const auto& absent = provider.snapshot().reading(SysmonMetric::SwapPct);
    require(
        !absent.current && !absent.gauge && absent.history.empty() && absent.absence == SysmonAbsence::NotConfigured,
        "no swap fabricated usage or retained history"
    );
    require(formatSysmonValue(*findSysmonMetric("swap_pct"), absent) == "No swap", "no swap not distinguishable");
    fixture.write("meminfo", "SwapTotal: 1000 kB\nSwapFree: 1000 kB\n");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::SwapPct).current, 0, "enabling swap did not produce real zero");
    require(
        provider.snapshot().reading(SysmonMetric::SwapPct).absence == SysmonAbsence::Unavailable,
        "enabling swap retained not-configured status"
    );
    std::filesystem::remove(fixture.root / "meminfo");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::SwapPct));
    fixture.write("meminfo", std::string(65537, 'x'));
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::SwapPct));

    provider.setDemand({false, false, true});
    for (const auto free : {std::uint64_t{1000}, std::uint64_t{800}, std::uint64_t{200}, std::uint64_t{0}}) {
      fixture.write("meminfo", std::format("SwapTotal: 1000 kB\nSwapFree: {} kB\n", free));
      provider.sample();
      const auto& reading = provider.snapshot().reading(SysmonMetric::SwapPct);
      near(*reading.current, static_cast<double>(1000 - free) / 10, "swap boundary value incorrect");
      require(
          reading.absence == SysmonAbsence::Unavailable && reading.gauge.has_value(),
          "swap did not recover from absence"
      );
      near(reading.gauge->highlight, free >= 800 ? 0 : 1, "swap threshold boundary incorrect");
    }
    near(*retained.reading(SysmonMetric::SwapPct).current, 50, "retained swap snapshot mutated");
    provider.setDemand({});
    unavailable(provider.snapshot().reading(SysmonMetric::SwapPct));
    provider.setDemand({true, true, true});
    fixture.cpu(0, 0);
    for (std::uint64_t i = 0; i <= 125; ++i) {
      fixture.cpu(i, i);
      fixture.write(
          "meminfo",
          std::format("MemTotal: 1000 kB\nMemAvailable: 500 kB\nSwapTotal: 1000 kB\nSwapFree: {} kB\n", 1000 - i)
      );
      provider.sample();
    }
    const auto& history = provider.snapshot().reading(SysmonMetric::SwapPct).history;
    require(history.size() == 120, "swap history exceeded bound");
    near(history.front(), 0.006, "oldest swap history sample incorrect");
    near(history.back(), 0.125, "latest swap history sample incorrect");
    const auto ramHistorySize = provider.snapshot().reading(SysmonMetric::RamPct).history.size();
    provider.setDemand({true, true, false});
    unavailable(provider.snapshot().reading(SysmonMetric::SwapPct));
    require(
        provider.snapshot().reading(SysmonMetric::RamPct).history.size() == ramHistorySize, "dropping swap cleared RAM"
    );
    fixture.cpu(126, 126);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "dropping swap cleared CPU baseline");
    provider.setDemand({false, false, true});
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::SwapPct).history.size() == 1, "resuming swap retained paused history"
    );
  }

  void checkFrequency(const Fixture& fixture) {
    using namespace greeter;
    const auto cpuRoot = fixture.root / "cpus";
    CpuRamProvider provider(fixture.root, cpuRoot);
    provider.setDemand({false, false, false, true});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    fixture.frequency("cpu0", "2000000\n", "4000000\n");
    fixture.frequency("cpu3", "4000000\n", "5000000\n");
    fixture.frequency("cpu1", "0\n", "0\n");
    fixture.frequency("cpu2", "broken", "0");
    for (const auto* name : {"cpu", "cpufreq", "cpu3-extra", "gpu0"})
      fixture.frequency(name, "999000000", "999000000");
    provider.sample();
    const auto retained = provider.snapshot();
    const auto& initial = retained.reading(SysmonMetric::CpuFreq);
    near(*initial.current, 3000, "frequency did not average readable nonzero cores");
    near(initial.gauge->normalized, 0.6, "frequency did not use highest reported ceiling");
    near(initial.gauge->highlight, 0.4375, "frequency highlight used wrong units or thresholds");
    require(formatSysmonValue(*findSysmonMetric("cpu_freq"), initial) == "3.0 GHz", "frequency display unit incorrect");
    unavailable(retained.reading(SysmonMetric::CpuUsage));
    unavailable(retained.reading(SysmonMetric::RamPct));
    unavailable(retained.reading(SysmonMetric::SwapPct));
    provider.setDemand({false, false, false, true});
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::CpuFreq).history.size() == 2, "same frequency demand reset history"
    );
    std::filesystem::remove_all(cpuRoot);
    for (const auto text : std::array<std::string_view, 10>{
             "", " \t\n", "0", "-1", "+1000", "1000x", "1000 2000", "1e6", "1.5", "18446744073709551616"
         }) {
      fixture.frequency("cpu0", text, "5000000");
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    }
    fixture.frequency("cpu0", std::string(65, '1'), "5000000");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    for (const auto text : std::array<std::string_view, 5>{"", "0", "broken", "-1", "18446744073709551616"}) {
      fixture.frequency("cpu0", " \t2500000\n", text);
      provider.sample();
      const auto& reading = provider.snapshot().reading(SysmonMetric::CpuFreq);
      near(*reading.current, 2500, "missing ceiling hid readable frequency");
      require(!reading.gauge && reading.history.empty(), "frequency invented a normalization ceiling");
    }
    fixture.frequency("cpu0", "3000000", "2000000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuFreq).current, 3000, "boosted raw frequency clipped");
    near(provider.snapshot().reading(SysmonMetric::CpuFreq).gauge->normalized, 1, "boosted frequency fill not bounded");
    for (const auto khz : {2500000, 3500000, 4500000}) {
      fixture.frequency("cpu0", std::to_string(khz), "5000000");
      provider.sample();
      near(
          provider.snapshot().reading(SysmonMetric::CpuFreq).gauge->highlight,
          khz == 2500000       ? 0
              : khz == 3500000 ? 0.625
                               : 1,
          "frequency gradient boundary incorrect"
      );
    }
    fixture.frequency("cpu0", "2500000", "18446744073709551615");
    provider.sample();
    require(provider.snapshot().reading(SysmonMetric::CpuFreq).gauge.has_value(), "valid large ceiling overflowed");
    fixture.frequency("cpu0", "2500000", "broken");
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::CpuFreq).history.empty(), "lost ceiling retained normalized history"
    );
    std::filesystem::remove(cpuRoot / "cpu0/cpufreq/scaling_cur_freq");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    near(*retained.reading(SysmonMetric::CpuFreq).current, 3000, "retained frequency snapshot mutated");
    provider.setDemand({});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    provider.setDemand({false, false, false, true});
    for (int i = 0; i <= 125; ++i) {
      fixture.frequency("cpu0", std::to_string(1000000 + i * 1000), "2000000");
      provider.sample();
    }
    const auto& history = provider.snapshot().reading(SysmonMetric::CpuFreq).history;
    require(history.size() == 120, "frequency history exceeded bound");
    near(history.front(), 0.503, "oldest frequency sample incorrect");
    near(history.back(), 0.5625, "latest frequency sample incorrect");
    provider.setDemand({true, true, true, true});
    fixture.cpu(0, 0);
    fixture.write("meminfo", "MemTotal: 1000 kB\nMemAvailable: 500 kB\nSwapTotal: 1000 kB\nSwapFree: 800 kB\n");
    provider.sample();
    provider.setDemand({true, true, true, false});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuFreq));
    fixture.cpu(1, 1);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "dropping frequency cleared CPU baseline");
    near(*provider.snapshot().reading(SysmonMetric::RamPct).current, 50, "dropping frequency cleared RAM");
    near(*provider.snapshot().reading(SysmonMetric::SwapPct).current, 20, "dropping frequency cleared swap");
    provider.setDemand({false, false, false, true});
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::CpuFreq).history.size() == 1, "resuming frequency retained history"
    );
  }

  void checkTemperature(const Fixture& fixture) {
    using namespace greeter;
    const auto hwmon = fixture.root / "hwmon";
    const auto thermal = fixture.root / "thermal";
    CpuRamProvider provider(fixture.root, fixture.root / "cpus", hwmon, thermal);
    provider.setDemand({.temperature = true});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.sensor("hwmon0", "nvme", "CPU", "99000");
    fixture.sensor("hwmon1", "amdgpu", "CPU", "98000");
    fixture.zone("thermal_zone9", "gpu-thermal", "97000");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.sensor("hwmon5", "k10temp", "Tccd1", "45000", 3);
    fixture.sensor("hwmon5", "k10temp", "Tctl", "55000", 1);
    fixture.sensor("hwmon2", "coretemp", "Package id 0", "65000");
    provider.sample();
    const auto retained = provider.snapshot();
    near(*retained.reading(SysmonMetric::CpuTemp).current, 55, "CPU driver/package selection incorrect");
    near(retained.reading(SysmonMetric::CpuTemp).gauge->normalized, 0.5, "initial temperature range incorrect");
    near(retained.reading(SysmonMetric::CpuTemp).gauge->highlight, 0, "cool temperature highlighted");
    unavailable(retained.reading(SysmonMetric::CpuUsage));
    provider.setDemand({.temperature = true});
    fixture.sensor("hwmon5", "k10temp", "Tctl", "100000");
    provider.sample();
    const auto& expanded = provider.snapshot().reading(SysmonMetric::CpuTemp);
    require(expanded.history.size() == 2, "same temperature demand reset history");
    near(expanded.history.front(), 25.0 / 70.0, "range expansion did not renormalize existing history");
    near(expanded.gauge->normalized, 1, "temperature upper range did not expand");
    near(expanded.gauge->highlight, 1, "critical temperature not highlighted");
    fixture.sensor("hwmon5", "k10temp", "Tctl", "0");
    provider.sample();
    near(
        *provider.snapshot().reading(SysmonMetric::CpuTemp).current, 45, "zero CPU candidate hid valid nonzero sensor"
    );
    require(
        provider.snapshot().reading(SysmonMetric::CpuTemp).history.size() == 1, "sensor change retained old history"
    );
    std::filesystem::remove_all(hwmon);
    fixture.sensor("hwmon0", "coretemp", "Core 0", "40000", 2);
    fixture.sensor("hwmon0", "coretemp", "Package id 0", "60000", 3);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 60, "Intel package not preferred");
    std::filesystem::remove_all(hwmon);
    fixture.sensor("hwmon0", "zenpower", "Tdie", "50000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 50, "zenpower sensor unsupported");
    fixture.sensor("hwmon0", "ibmpowernv", "CORE temperature", "62000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 62, "Power CPU sensor unsupported");
    require(
        provider.snapshot().reading(SysmonMetric::CpuTemp).history.size() == 1,
        "reused sensor path ignored identity change"
    );
    std::filesystem::remove_all(hwmon);
    fixture.zone("thermal_zone0", "acpitz", "64000");
    fixture.zone("thermal_zone1", "x86_pkg_temp", "63000");
    fixture.zone("thermal_zone2", "cpu-thermal", "61000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 61, "known thermal zone priority incorrect");
    std::filesystem::remove_all(thermal);
    fixture.sensor("hwmon0", "k10temp", "Tctl", "500");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 0.5, "millidegrees guessed to be Celsius");
    fixture.sensor("hwmon0", "k10temp", "Tctl", "-500");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, -0.5, "signed millidegrees rejected");
    for (const auto* text :
         {"", "broken", "50000x", "50000 60000", "NaN", "+50000", "9223372036854775808", "-273151"}) {
      fixture.sensor("hwmon0", "k10temp", "Tctl", text);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    }
    fixture.sensor("hwmon0", "k10temp", "Tctl", std::string(257, '1'));
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.sensor("hwmon0", "k10temp", "Tctl", "50000");
    fixture.write("hwmon/hwmon0/temp1_fault", "1");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.write("hwmon/hwmon0/temp1_fault", "broken");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.write("hwmon/hwmon0/temp1_fault", "0");
    fixture.write("hwmon/hwmon0/temp1_enable", "0");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.write("hwmon/hwmon0/temp1_enable", "1");
    for (const auto raw : {0, 60000, 72500, 85000}) {
      fixture.sensor("hwmon0", "k10temp", "Tctl", std::to_string(raw));
      provider.sample();
      near(
          *provider.snapshot().reading(SysmonMetric::CpuTemp).current, static_cast<double>(raw) / 1000,
          "temperature zero/boundary value incorrect"
      );
      near(
          provider.snapshot().reading(SysmonMetric::CpuTemp).gauge->highlight,
          raw <= 60000       ? 0
              : raw == 72500 ? 0.625
                             : 1,
          "temperature highlight boundary incorrect"
      );
    }
    near(*retained.reading(SysmonMetric::CpuTemp).current, 55, "retained temperature snapshot mutated");
    provider.setDemand({});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    provider.setDemand({.temperature = true});
    for (int i = 0; i <= 125; ++i) {
      fixture.sensor("hwmon0", "k10temp", "Tctl", std::to_string(40000 + i * 100));
      provider.sample();
    }
    const auto& history = provider.snapshot().reading(SysmonMetric::CpuTemp).history;
    require(history.size() == 120, "temperature history exceeded bound");
    near(history.front(), 0.212, "oldest temperature sample incorrect");
    near(history.back(), 0.45, "latest temperature sample incorrect");
    provider.setDemand({true, true, true, true, true});
    fixture.cpu(0, 0);
    fixture.write("meminfo", "MemTotal: 1000 kB\nMemAvailable: 500 kB\nSwapTotal: 1000 kB\nSwapFree: 800 kB\n");
    provider.sample();
    provider.setDemand({true, true, true, true, false});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuTemp));
    fixture.cpu(1, 1);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "dropping temperature reset CPU baseline");
    require(
        provider.snapshot().reading(SysmonMetric::CpuFreq).current.has_value(), "dropping temperature cleared frequency"
    );
    provider.setDemand({.temperature = true});
    fixture.sensor("hwmon0", "k10temp", "Tctl", "55000");
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::CpuTemp).history.size() == 1, "resumed temperature retained history"
    );
    near(
        provider.snapshot().reading(SysmonMetric::CpuTemp).gauge->normalized, 0.5,
        "resumed temperature retained old range"
    );
  }

  void checkGpuTemperature(const Fixture& fixture) {
    using namespace greeter;
    const auto hwmon = fixture.root / "hwmon";
    CpuRamProvider provider(fixture.root, fixture.root / "cpus", hwmon, fixture.root / "thermal");
    provider.setDemand({.gpuTemperature = true});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.gpu("hwmon1", "nvme", "GPU", "99000");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.gpu("hwmon2", "amdgpu", "edge", "55000");
    provider.sample();
    const auto retained = provider.snapshot();
    near(*retained.reading(SysmonMetric::GpuTemp).current, 55, "GPU edge reading incorrect");
    near(retained.reading(SysmonMetric::GpuTemp).gauge->normalized, 0.5, "GPU default normalization incorrect");
    unavailable(retained.reading(SysmonMetric::CpuTemp));
    provider.setDemand({.gpuTemperature = true});
    provider.sample();
    require(provider.snapshot().reading(SysmonMetric::GpuTemp).history.size() == 2, "same GPU demand reset history");
    fixture.gpu("hwmon2", "amdgpu", "junction", "75000", "active", 2);
    fixture.gpu("hwmon2", "amdgpu", "mem", "99000", "active", 3);
    provider.sample();
    near(
        *provider.snapshot().reading(SysmonMetric::GpuTemp).current, 75, "GPU sensor scoring differs from desktop hwmon"
    );
    require(
        provider.snapshot().reading(SysmonMetric::GpuTemp).history.size() == 1,
        "GPU channel switch retained old history"
    );
    fixture.gpu("hwmon3", "amdgpu", "edge", "80000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::GpuTemp).current, 80, "hottest equally ranked GPU not selected");
    fixture.gpu("hwmon2", "amdgpu", "junction", "80000", "active", 2);
    provider.sample();
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::GpuTemp).history.size() == 2,
        "equal GPU candidates did not select deterministically"
    );
    fixture.gpu("hwmon4", "nouveau", "junction", "100000");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::GpuTemp).current, 80, "GPU driver scoring changed");
    std::filesystem::remove_all(hwmon);
    for (const auto* driver : {"amdgpu", "nvidia", "nouveau", "i915", "xe"}) {
      fixture.gpu("hwmon0", driver, "GPU", "55000");
      provider.sample();
      near(*provider.snapshot().reading(SysmonMetric::GpuTemp).current, 55, "known GPU hwmon driver not supported");
    }
    fixture.gpu("hwmon0", "amdgpu", "edge", "55000");
    for (const auto* status : {"suspended", "suspending", "resuming", "error", "unknown", ""}) {
      fixture.gpu("hwmon0", "amdgpu", "edge", "55000", status);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    }
    std::filesystem::remove(hwmon / "hwmon0/device/power/runtime_status");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.gpu("hwmon0", "amdgpu", "edge", "55000", "unsupported");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::GpuTemp).current, 55, "runtime-PM-disabled GPU was excluded");
    for (const auto* raw : {"", "broken", "50000x", "50000 60000", "NaN", "-273151", "9223372036854775808"}) {
      fixture.gpu("hwmon0", "amdgpu", "edge", raw);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    }
    fixture.gpu("hwmon0", "amdgpu", "edge", std::string(257, '1'));
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    for (const auto raw : {0, -500, 500, 60000, 72500, 85000}) {
      fixture.gpu("hwmon0", "amdgpu", "edge", std::to_string(raw));
      provider.sample();
      near(
          *provider.snapshot().reading(SysmonMetric::GpuTemp).current, static_cast<double>(raw) / 1000,
          "GPU Celsius zero/signed/boundary value incorrect"
      );
      near(
          provider.snapshot().reading(SysmonMetric::GpuTemp).gauge->highlight,
          raw <= 60000       ? 0
              : raw == 72500 ? 0.625
                             : 1,
          "GPU highlight thresholds incorrect"
      );
    }
    fixture.write("hwmon/hwmon0/temp1_fault", "1");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.write("hwmon/hwmon0/temp1_fault", "0");
    fixture.write("hwmon/hwmon0/temp1_enable", "0");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.write("hwmon/hwmon0/temp1_enable", "1");
    provider.sample();
    require(
        provider.snapshot().reading(SysmonMetric::GpuTemp).history.size() == 1, "GPU recovery retained prior history"
    );
    provider.setDemand({});
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    provider.setDemand({.gpuTemperature = true});
    for (int i = 0; i <= 125; ++i) {
      fixture.gpu("hwmon0", "amdgpu", "edge", std::to_string(40000 + i * 100));
      provider.sample();
    }
    const auto& history = provider.snapshot().reading(SysmonMetric::GpuTemp).history;
    require(history.size() == 120, "GPU temperature history exceeded bound");
    near(history.front(), 0.212, "oldest GPU temperature sample incorrect");
    near(history.back(), 0.45, "latest GPU temperature sample incorrect");
    fixture.gpu("hwmon0", "amdgpu", "edge", "100000");
    provider.sample();
    near(
        provider.snapshot().reading(SysmonMetric::GpuTemp).history.front(), 10.7 / 70,
        "GPU range expansion left mixed normalization"
    );
    fixture.sensor("hwmon5", "k10temp", "Tctl", "50000");
    provider.setDemand({true, true, true, true, true, true});
    fixture.cpu(0, 0);
    provider.sample();
    provider.setDemand({true, true, true, true, true, false});
    unavailable(provider.snapshot().reading(SysmonMetric::GpuTemp));
    fixture.cpu(1, 1);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "dropping GPU reset CPU baseline");
    near(*provider.snapshot().reading(SysmonMetric::CpuTemp).current, 50, "dropping GPU reset CPU temperature");
    require(provider.snapshot().reading(SysmonMetric::CpuFreq).current.has_value(), "dropping GPU reset frequency");
    provider.setDemand({.gpuTemperature = true});
    fixture.gpu("hwmon0", "amdgpu", "edge", "55000");
    provider.sample();
    require(provider.snapshot().reading(SysmonMetric::GpuTemp).history.size() == 1, "resuming GPU retained history");
    near(provider.snapshot().reading(SysmonMetric::GpuTemp).gauge->normalized, 0.5, "resuming GPU retained range");
    near(*retained.reading(SysmonMetric::GpuTemp).current, 55, "published GPU snapshot mutated later");
  }
} // namespace

int main() {
  try {
    using namespace greeter;
    Fixture fixture;
    CpuRamProvider provider(fixture.root);
    unavailable(provider.sample().reading(SysmonMetric::CpuUsage));
    unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    fixture.cpu(100, 900);
    fixture.ram(1000, 400);
    provider.setDemand({true, true});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    near(*provider.snapshot().reading(SysmonMetric::RamPct).current, 60, "RAM did not use available memory");
    fixture.cpu(125, 975);
    const auto retained = provider.sample();
    near(*retained.reading(SysmonMetric::CpuUsage).current, 25, "CPU delta or guest accounting incorrect");
    near(retained.reading(SysmonMetric::CpuUsage).gauge->normalized, 0.25, "CPU gauge normalization incorrect");
    near(retained.reading(SysmonMetric::CpuUsage).gauge->highlight, 0, "low CPU value highlighted");
    require(retained.reading(SysmonMetric::CpuUsage).history.size() == 1, "warmup invented a history sample");
    for (const auto metric :
         {SysmonMetric::CpuTemp, SysmonMetric::CpuFreq, SysmonMetric::GpuTemp, SysmonMetric::GpuUsage,
          SysmonMetric::GpuVram, SysmonMetric::GpuVramUsed, SysmonMetric::SwapPct})
      unavailable(retained.reading(metric));
    provider.setDemand({true, true});
    fixture.cpu(195, 1005);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 70, "same demand reset CPU baseline");
    near(
        provider.snapshot().reading(SysmonMetric::CpuUsage).gauge->highlight, 0.625, "CPU gradient differs from desktop"
    );
    fixture.ram(1000, 250);
    provider.sample();
    near(
        provider.snapshot().reading(SysmonMetric::RamPct).gauge->highlight, 0.625, "RAM gradient differs from desktop"
    );
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    near(*retained.reading(SysmonMetric::CpuUsage).current, 25, "published value copy changed later");

    provider.setDemand({false, true});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    require(provider.snapshot().reading(SysmonMetric::RamPct).current.has_value(), "dropping CPU reset RAM");
    provider.setDemand({true, true});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    fixture.cpu(295, 1005);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 100, "full CPU value incorrect");
    near(provider.snapshot().reading(SysmonMetric::CpuUsage).gauge->highlight, 1, "critical CPU value not highlighted");
    fixture.cpu(295, 1105);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 0, "idle CPU confused with unavailable");
    fixture.cpu(1000, 500);
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    fixture.cpu(1000, 600);
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 0, "counter reset did not reprime");

    for (const auto line : std::array<std::string_view, 9>{
             "", "cpu0 1 2 3 4", "cpu 1 2 3", "cpu -1 0 0 0", "cpu 1x 0 0 0", "cpu 18446744073709551616 0 0 0",
             "cpu 18446744073709551615 0 0 1", "cpu 1 2 3 4 broken", "cpu "
         }) {
      fixture.write("stat", line);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
      require(provider.snapshot().reading(SysmonMetric::RamPct).current.has_value(), "CPU error poisoned RAM");
      fixture.cpu(0, 0);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
      fixture.cpu(1, 1);
      provider.sample();
      near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "CPU failed to recover after read error");
    }
    fixture.write("stat", "cpu " + std::string(4096, '0'));
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    fixture.write("stat", "cpu 0 0 0 0\n");
    provider.sample();
    fixture.write("stat", "cpu 1 0 0 1\n");
    provider.sample();
    near(*provider.snapshot().reading(SysmonMetric::CpuUsage).current, 50, "four mandatory counters rejected");
    std::filesystem::remove(fixture.root / "stat");
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));

    provider.setDemand({false, true});
    for (const auto text : std::array<std::string_view, 11>{
             "", "MemTotal: 1000 kB\n", "MemAvailable: 100 kB\n", "MemTotal: 0 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 10 kB\nMemAvailable: 11 kB\n", "MemTotal: -1 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 10 MB\nMemAvailable: 0 kB\n", "MemTotal: 10 kB\nMemAvailable: 0 kB junk\n",
             "MemTotal: 10 kB\nMemTotal: 10 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 18446744073709551616 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 10 kB\nMemAvailable: 1 kB\nMemAvailable: 2 kB\n"
         }) {
      fixture.write("meminfo", text);
      provider.sample();
      unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    }
    fixture.write("meminfo", std::string(65537, 'x'));
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    for (const auto available : {std::uint64_t{0}, std::uint64_t{1000}}) {
      fixture.ram(1000, available);
      provider.sample();
      near(
          *provider.snapshot().reading(SysmonMetric::RamPct).current, available == 0 ? 100 : 0, "RAM boundary rejected"
      );
    }
    provider.setDemand({});
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    provider.setDemand({true, true});
    fixture.cpu(0, 0);
    fixture.ram(1000, 500);
    provider.sample();
    for (std::uint64_t i = 1; i <= 125; ++i) {
      fixture.cpu(i, i);
      fixture.ram(1000, 500 - i);
      provider.sample();
    }
    const auto& cpu = provider.snapshot().reading(SysmonMetric::CpuUsage);
    const auto& ram = provider.snapshot().reading(SysmonMetric::RamPct);
    require(cpu.history.size() == 120 && ram.history.size() == 120, "history was not bounded");
    near(ram.history.front(), 0.506, "oldest retained history sample incorrect");
    near(ram.history.back(), 0.625, "latest history sample incorrect");
    provider.setDemand({});
    provider.sample();
    unavailable(provider.snapshot().reading(SysmonMetric::CpuUsage));
    unavailable(provider.snapshot().reading(SysmonMetric::RamPct));
    checkSwap(fixture);
    checkFrequency(fixture);
    checkTemperature(fixture);
    checkGpuTemperature(fixture);
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "cpu-ram-provider-test: {}", error.what());
    return 1;
  }
}
