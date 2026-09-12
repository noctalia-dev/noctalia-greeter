#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_io.h"
#include "tools/secure_appearance_sync.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {

  constexpr std::string_view kStagingName = "noctalia-greeter-sync";

  class Fixture {
  public:
    Fixture(const mode_t stagingMode, const mode_t fileMode) {
      std::array<char, 64> pathTemplate{};
      const std::string pattern = "/tmp/noctalia-legacy-staging-test.XXXXXX";
      std::copy(pattern.begin(), pattern.end(), pathTemplate.begin());
      char* created = ::mkdtemp(pathTemplate.data());
      if (created == nullptr) {
        throw std::runtime_error(std::string("mkdtemp failed: ") + std::strerror(errno));
      }

      runtimeParent = created;
      runtimeDirectory = runtimeParent / std::to_string(::getuid());
      stagingDirectory = runtimeDirectory / kStagingName;
      syncFile = stagingDirectory / "sync.toml";

      chmodOrThrow(runtimeParent, 0755);
      std::filesystem::create_directory(runtimeDirectory);
      chmodOrThrow(runtimeDirectory, 0700);
      std::filesystem::create_directory(stagingDirectory);
      chmodOrThrow(stagingDirectory, stagingMode);
      std::ofstream(syncFile) << "[appearance]\n";
      chmodOrThrow(syncFile, fileMode);
    }

    ~Fixture() {
      std::error_code ec;
      std::filesystem::remove_all(runtimeParent, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    static void chmodOrThrow(const std::filesystem::path& path, const mode_t mode) {
      if (::chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error("chmod failed for " + path.string() + ": " + std::strerror(errno));
      }
    }

    std::filesystem::path runtimeParent;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path stagingDirectory;
    std::filesystem::path syncFile;
  };

  [[nodiscard]] bool validate(
      const Fixture& fixture, const std::filesystem::path& stagingDirectory, const std::optional<uid_t> invokingUid,
      std::string& error
  ) {
    error.clear();
    return greeter::secure_sync::detail::validateLegacyStagingForTesting(
        stagingDirectory, fixture.runtimeParent, ::getuid(), invokingUid, error
    );
  }

  [[nodiscard]] bool validateConstrained(const Fixture& fixture, std::string& error) {
    error.clear();
    return greeter::secure_sync::detail::validateConstrainedStagingForTesting(
        fixture.stagingDirectory, fixture.runtimeParent, ::getuid(), ::getuid(), error
    );
  }

  void expect(std::string_view name, const bool actual, const bool expected, const std::string& error, bool& passed) {
    if (actual == expected) {
      return;
    }
    std::cerr << name << ": expected " << expected << ", got " << actual;
    if (!error.empty()) {
      std::cerr << " (" << error << ')';
    }
    std::cerr << '\n';
    passed = false;
  }

  void expectFillMode(
      const std::string_view name, const std::string_view value, const std::optional<WallpaperFillMode> expected,
      bool& passed
  ) {
    const auto actual = greeter::appearance::parseFillMode(value);
    if (actual == expected) {
      return;
    }
    std::cerr << name << ": unexpected fill-mode parse result\n";
    passed = false;
  }

  void writeSpanSyncToml(const std::filesystem::path& path) {
    std::ofstream(path, std::ios::trunc) << R"toml(
[appearance]
scheme = "Synced"

[appearance.wallpaper]
path = "color:#010203"
fill_mode = "span"

[appearance.wallpapers.DP-1]
path = "color:#040506"
fill_mode = "span"

[appearance.palette]
primary = "#010101"
on_primary = "#020202"
secondary = "#030303"
on_secondary = "#040404"
tertiary = "#050505"
on_tertiary = "#060606"
error = "#070707"
on_error = "#080808"
surface = "#090909"
on_surface = "#101010"
surface_variant = "#111111"
on_surface_variant = "#121212"
outline = "#131313"
shadow = "#141414"
hover = "#151515"
on_hover = "#161616"
)toml";
  }

} // namespace

int main() {
  bool passed = true;
  std::string error;

  try {
    expectFillMode("center fill mode", "center", WallpaperFillMode::Center, passed);
    expectFillMode("crop fill mode", "crop", WallpaperFillMode::Crop, passed);
    expectFillMode("fit fill mode", "fit", WallpaperFillMode::Fit, passed);
    expectFillMode("stretch fill mode", "stretch", WallpaperFillMode::Stretch, passed);
    expectFillMode("repeat fill mode", "repeat", WallpaperFillMode::Repeat, passed);
    expectFillMode("span fill mode", "span", WallpaperFillMode::Span, passed);
    expectFillMode("invalid fill mode", "tile", std::nullopt, passed);

    {
      Fixture fixture(0700, 0600);
      writeSpanSyncToml(fixture.syncFile);
      greeter::config::clearConfigDiagnostics();
      const auto sync = greeter::config::loadSync(fixture.syncFile);
      expect("span sync.toml parses cleanly", greeter::config::configDiagnostics().empty(), true, {}, passed);

      const bool defaultSpan = sync.appearance.wallpaper.has_value()
          && sync.appearance.wallpaper->fillMode.has_value()
          && greeter::appearance::parseFillMode(*sync.appearance.wallpaper->fillMode) == WallpaperFillMode::Span;
      expect("default staged span wallpaper", defaultSpan, true, {}, passed);

      const auto outputWallpaper = sync.appearance.wallpapers.find("DP-1");
      const bool outputSpan = outputWallpaper != sync.appearance.wallpapers.end()
          && outputWallpaper->second.fillMode.has_value()
          && greeter::appearance::parseFillMode(*outputWallpaper->second.fillMode) == WallpaperFillMode::Span;
      expect("per-output staged span wallpaper", outputSpan, true, {}, passed);

      error.clear();
      expect(
          "constrained sync accepts default and per-output span",
          greeter::secure_sync::detail::validateConstrainedPayloadForTesting(fixture.stagingDirectory, error), true,
          error, passed
      );
    }

    {
      // Noctalia 5.0.1 inherits umask. With umask 0002 it creates this
      // 0775 directory and 0664 payload beneath its private 0700 runtime dir.
      Fixture fixture(0775, 0664);
      expect(
          "private runtime with matching caller", validate(fixture, fixture.stagingDirectory, ::getuid(), error), true,
          error, passed
      );
      expect(
          "private runtime without caller environment",
          validate(fixture, fixture.stagingDirectory, std::nullopt, error), true, error, passed
      );
      expect("constrained sync remains strict", validateConstrained(fixture, error), false, error, passed);

      const uid_t otherUid = ::getuid() == std::numeric_limits<uid_t>::max() ? ::getuid() - 1 : ::getuid() + 1;
      expect("mismatched caller", validate(fixture, fixture.stagingDirectory, otherUid, error), false, error, passed);
    }

    {
      Fixture fixture(0775, 0664);
      const auto custom = fixture.runtimeParent / "custom-staging";
      std::filesystem::create_directory(custom);
      Fixture::chmodOrThrow(custom, 0775);
      const auto config = custom / "sync.toml";
      std::ofstream(config) << "[appearance]\n";
      Fixture::chmodOrThrow(config, 0664);
      expect("group-writable custom path", validate(fixture, custom, ::getuid(), error), false, error, passed);

      Fixture::chmodOrThrow(custom, 0700);
      Fixture::chmodOrThrow(config, 0600);
      expect("strict custom path", validate(fixture, custom, ::getuid(), error), true, error, passed);
    }

    {
      Fixture fixture(0700, 0600);
      expect("private constrained staging", validateConstrained(fixture, error), true, error, passed);
    }

    {
      Fixture fixture(0700, 0600);
      Fixture::chmodOrThrow(fixture.runtimeDirectory, 0710);
      expect(
          "non-private runtime does not fall back", validate(fixture, fixture.stagingDirectory, ::getuid(), error),
          false, error, passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      Fixture::chmodOrThrow(fixture.runtimeParent, 0777);
      expect(
          "writable runtime parent", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error,
          passed
      );
    }

    {
      Fixture fixture(0777, 0664);
      expect(
          "world-writable private staging", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false,
          error, passed
      );
    }

    {
      Fixture fixture(0775, 0666);
      expect(
          "world-writable private file", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error,
          passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      std::filesystem::create_hard_link(fixture.syncFile, fixture.runtimeDirectory / "linked-sync.toml");
      expect(
          "group-writable hard link", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error,
          passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      const auto target = fixture.runtimeDirectory / "target-staging";
      std::filesystem::rename(fixture.stagingDirectory, target);
      std::filesystem::create_directory_symlink(target, fixture.stagingDirectory);
      expect("symlinked staging", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error, passed);
    }

    {
      Fixture fixture(0775, 0664);
      const auto target = fixture.runtimeParent / "target-runtime";
      std::filesystem::rename(fixture.runtimeDirectory, target);
      std::filesystem::create_directory_symlink(target, fixture.runtimeDirectory);
      expect(
          "symlinked runtime directory", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error,
          passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      const auto target = fixture.runtimeDirectory / "target-sync.toml";
      std::filesystem::rename(fixture.syncFile, target);
      std::filesystem::create_symlink(target, fixture.syncFile);
      expect(
          "symlinked staged file", validate(fixture, fixture.stagingDirectory, ::getuid(), error), false, error, passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      const auto nested = fixture.runtimeDirectory / "nested" / kStagingName;
      std::filesystem::create_directories(nested);
      Fixture::chmodOrThrow(nested, 0775);
      const auto config = nested / "sync.toml";
      std::ofstream(config) << "[appearance]\n";
      Fixture::chmodOrThrow(config, 0664);
      expect(
          "nested path receives no compatibility relaxation", validate(fixture, nested, ::getuid(), error), false,
          error, passed
      );
    }

    {
      Fixture fixture(0775, 0664);
      const std::string paddedUid = "0" + std::to_string(::getuid());
      const auto paddedRuntime = fixture.runtimeParent / paddedUid;
      const auto paddedStaging = paddedRuntime / kStagingName;
      std::filesystem::create_directories(paddedStaging);
      Fixture::chmodOrThrow(paddedRuntime, 0700);
      Fixture::chmodOrThrow(paddedStaging, 0775);
      const auto config = paddedStaging / "sync.toml";
      std::ofstream(config) << "[appearance]\n";
      Fixture::chmodOrThrow(config, 0664);
      expect(
          "non-canonical uid receives no compatibility relaxation", validate(fixture, paddedStaging, ::getuid(), error),
          false, error, passed
      );
    }
  } catch (const std::exception& exception) {
    std::cerr << "fixture setup failed: " << exception.what() << '\n';
    passed = false;
  }

  return passed ? 0 : 1;
}
