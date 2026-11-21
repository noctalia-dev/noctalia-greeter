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
  };

  void near(double value, double expected, const char* message) {
    require(std::abs(value - expected) < 0.0001, message);
  }
  void unavailable(const greeter::SysmonReading& reading) {
    require(!reading.current && !reading.gauge && reading.history.empty(), "unavailable metric retained data");
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
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "cpu-ram-provider-test: {}", error.what());
    return 1;
  }
}
