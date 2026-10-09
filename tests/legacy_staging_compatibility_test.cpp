#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_io.h"
#include "greeter/greeter_preferences.h"
#include "tools/secure_appearance_sync.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
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
  constexpr std::string_view kSyncedOutputLayout = "DP-1:0,0; DP-2:1280,0";
  constexpr std::string_view kSyncedOutputTransforms = "DP-1:90; DP-2:normal";
  constexpr std::string_view kSyncedOutputScales = "DP-1:2; DP-2:1.25";

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

  class ScopedStateDirectory {
  public:
    explicit ScopedStateDirectory(const std::filesystem::path& path) {
      if (const char* current = std::getenv(greeter::appearance::kSyncedDataDirEnv)) {
        previous = current;
      }
      if (::setenv(greeter::appearance::kSyncedDataDirEnv, path.c_str(), 1) != 0) {
        throw std::runtime_error("setenv failed");
      }
    }

    ~ScopedStateDirectory() {
      if (previous.has_value()) {
        ::setenv(greeter::appearance::kSyncedDataDirEnv, previous->c_str(), 1);
      } else {
        ::unsetenv(greeter::appearance::kSyncedDataDirEnv);
      }
    }

    ScopedStateDirectory(const ScopedStateDirectory&) = delete;
    ScopedStateDirectory& operator=(const ScopedStateDirectory&) = delete;

  private:
    std::optional<std::string> previous;
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

  void expectOutputMappings(
      const std::string_view name, const greeter_compositor_config& config, const std::string_view layout,
      const std::string_view transforms, const std::string_view scales, bool& passed
  ) {
    expect(
        std::string(name) + " layout", std::string_view(config.output_layout) == layout, true,
        std::string(config.output_layout), passed
    );
    expect(
        std::string(name) + " transforms", std::string_view(config.output_transforms) == transforms, true,
        std::string(config.output_transforms), passed
    );
    expect(
        std::string(name) + " scales", std::string_view(config.output_scales) == scales, true,
        std::string(config.output_scales), passed
    );
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
      std::ofstream(fixture.runtimeDirectory / "greeter.toml") << R"toml(
[appearance]
scheme = "Noctalia"

[appearance.wallpaper]
path = "/nix/store/example-wallpaper.png"
fill_mode = "center"
)toml";

      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      const auto appearance = loadGreeterWallpaperAppearance();
      const auto wallpaper = appearance.has_value() ? appearance->wallpaperForOutput("") : std::nullopt;
      expect("wallpaper loads without synced palette", wallpaper.has_value(), true, {}, passed);
      expect(
          "wallpaper path loads without synced palette",
          wallpaper.has_value() && wallpaper->path == "/nix/store/example-wallpaper.png", true, {}, passed
      );
      expect(
          "wallpaper mode loads without synced palette",
          wallpaper.has_value() && wallpaper->fillMode == WallpaperFillMode::Center, true, {}, passed
      );
    }

    {
      Fixture fixture(0700, 0600);
      std::ofstream(fixture.runtimeDirectory / "greeter.toml") << R"toml(
[appearance]
scheme = "Noctalia"

[appearance.wallpaper]
fill_color = "#ff0000"
)toml";

      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      const auto appearance = loadGreeterWallpaperAppearance();
      const auto wallpaper = appearance.has_value() ? appearance->wallpaperForOutput("") : std::nullopt;
      const bool isRed = wallpaper.has_value() && wallpaper->path.empty() && wallpaper->fillColor == rgbHex(0xff0000);
      expect("fill-only wallpaper loads without synced palette", isRed, true, {}, passed);
    }

    {
      Fixture fixture(0700, 0600);
      std::ofstream(fixture.runtimeDirectory / "greeter.toml") << R"toml(
[clock]
enabled = false
position = "bottom-center"
time_format = ""
date_format = "{:%F}"
)toml";

      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      const auto preferences = greeter::loadGreeterPreferences();
      expect("clock enabled parses", preferences.clockEnabled, false, {}, passed);
      expect("clock position parses", preferences.clockPosition == "bottom-center", true, {}, passed);
      expect("empty clock format is preserved", preferences.clockTimeFormat.empty(), true, {}, passed);
      expect("clock date format parses", preferences.clockDateFormat == "{:%F}", true, {}, passed);

      const auto configPath = fixture.runtimeDirectory / "greeter.toml";
      const auto config = greeter::config::loadConfig(configPath);
      expect("clock config rewrites", greeter::config::writeConfig(configPath, config), true, {}, passed);
      const auto rewritten = greeter::config::loadConfig(configPath);
      expect(
          "empty clock format survives rewrite",
          rewritten.clockTimeFormat.has_value() && rewritten.clockTimeFormat->empty(), true, {}, passed
      );
    }

    {
      Fixture fixture(0700, 0600);
      std::ofstream(fixture.runtimeDirectory / "greeter.toml") << R"toml(
[output]
name = "Acer Technologies XV242Y TL1EE0018521"
width = 1920
height = 1080
refresh_rate = 120
)toml";

      greeter_compositor_config config{};
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expect(
          "stable output identifier parses",
          std::string_view(config.preferred_output) == "Acer Technologies XV242Y TL1EE0018521", true, {}, passed
      );
      expect("output width parses", config.manual_mode_width == 1920, true, {}, passed);
      expect("output height parses", config.manual_mode_height == 1080, true, {}, passed);
      expect("output refresh rate parses", config.manual_mode_refresh_mhz == 120000, true, {}, passed);
    }

    for (const std::string_view setting : {"", "use_synced_settings = true\n", "use_synced_settings = \"false\"\n"}) {
      Fixture fixture(0700, 0600);
      const auto configPath = fixture.runtimeDirectory / "greeter.toml";
      std::ofstream(configPath) << "[output]\n" << setting;
      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      expect(
          "stage synced output mappings",
          greeter::applyAppearanceSyncGreeterConf(
              std::string(kSyncedOutputLayout), std::string(kSyncedOutputTransforms), std::string(kSyncedOutputScales),
              std::nullopt
          ),
          true, {}, passed
      );

      greeter_compositor_config config{};
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expectOutputMappings(
          "enabled or omitted output sync", config, kSyncedOutputLayout, kSyncedOutputTransforms, kSyncedOutputScales,
          passed
      );
      const auto placements = greeter::loadGreeterOutputLayout();
      expect(
          "enabled or omitted client layout", placements.size() == 2 && placements[1].x == 1280, true,
          std::string(setting), passed
      );

      const auto declarative = greeter::config::loadConfig(configPath);
      expect(
          "invalid output sync type uses the enabled default", declarative.outputUseSyncedSettings.value_or(true), true,
          std::string(setting), passed
      );
      expect(
          "enabled output sync config rewrites", greeter::config::writeConfig(configPath, declarative), true, {}, passed
      );
      expect(
          "output sync setting survives rewrite",
          greeter::config::loadConfig(configPath).outputUseSyncedSettings == declarative.outputUseSyncedSettings, true,
          std::string(setting), passed
      );
    }

    {
      Fixture fixture(0700, 0600);
      const auto configPath = fixture.runtimeDirectory / "greeter.toml";
      std::ofstream(configPath) << "[output]\nuse_synced_settings = false\n";
      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      expect(
          "output sync stores metadata while opted out",
          greeter::applyAppearanceSyncGreeterConf(
              std::string(kSyncedOutputLayout), std::string(kSyncedOutputTransforms), std::string(kSyncedOutputScales),
              std::nullopt
          ),
          true, {}, passed
      );

      const auto declarative = greeter::config::loadConfig(configPath);
      expect("disabled output sync parses", declarative.outputUseSyncedSettings == false, true, {}, passed);
      expect(
          "disabled output sync config rewrites", greeter::config::writeConfig(configPath, declarative), true, {},
          passed
      );
      expect(
          "disabled output sync survives rewrite",
          greeter::config::loadConfig(configPath).outputUseSyncedSettings == false, true, {}, passed
      );

      greeter_compositor_config config{};
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expectOutputMappings("disabled output sync", config, "", "", "", passed);
      expect("disabled output sync leaves automatic scale", config.manual_scale == 0.0f, true, {}, passed);
      expect("disabled client layout", greeter::loadGreeterOutputLayout().empty(), true, {}, passed);

      writeSpanSyncToml(fixture.syncFile);
      std::ofstream(fixture.stagingDirectory / greeter::appearance::kOutputLayoutFileName)
          << "DP-1:50,60; DP-2:700,60\n";
      std::ofstream(fixture.stagingDirectory / greeter::appearance::kOutputTransformsFileName)
          << kSyncedOutputTransforms;
      std::ofstream(fixture.stagingDirectory / greeter::appearance::kOutputScalesFileName) << kSyncedOutputScales;
      error.clear();
      expect(
          "subsequent appearance sync succeeds with output opt-out",
          greeter::appearance::applySyncedGreeterPreferences(fixture.stagingDirectory, false, error), true, error,
          passed
      );
      expect("synced palette loads with output opt-out", loadGreeterSyncedAppearance().has_value(), true, {}, passed);
      expect(
          "synced wallpaper loads with output opt-out", loadGreeterWallpaperAppearance().has_value(), true, {}, passed
      );
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expectOutputMappings("output opt-out after appearance sync", config, "", "", "", passed);
      expect(
          "client layout remains opted out after appearance sync", greeter::loadGreeterOutputLayout().empty(), true, {},
          passed
      );

      auto preferences = greeter::loadGreeterPreferences();
      preferences.session = "Niri";
      expect("picker state saves with output opt-out", greeter::saveGreeterPreferences(preferences), true, {}, passed);
      const auto stored = greeter::config::loadSync(fixture.runtimeDirectory / "sync.toml");
      expect("synced layout remains stored", stored.outputLayout == "DP-1:50,60; DP-2:700,60", true, {}, passed);
      expect("synced transforms remain stored", stored.outputTransforms == kSyncedOutputTransforms, true, {}, passed);
      expect("synced scales remain stored", stored.outputScales == kSyncedOutputScales, true, {}, passed);

      auto enabled = greeter::config::loadConfig(configPath);
      enabled.outputUseSyncedSettings = true;
      expect("output sync can be re-enabled", greeter::config::writeConfig(configPath, enabled), true, {}, passed);
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expectOutputMappings(
          "re-enabled output sync", config, "DP-1:50,60; DP-2:700,60", kSyncedOutputTransforms, kSyncedOutputScales,
          passed
      );
      const auto placements = greeter::loadGreeterOutputLayout();
      expect(
          "re-enabled client layout", placements.size() == 2 && placements[0].x == 50 && placements[0].y == 60, true,
          {}, passed
      );
    }

    struct OutputStartupCase {
      const char* name;
      bool existingSync;
      bool compositorFirst;
    };
    for (const auto& startup : std::array{
             OutputStartupCase{"existing sync", true, true},
             OutputStartupCase{"first compositor startup", false, true},
             OutputStartupCase{"first client startup", false, false},
         }) {
      Fixture fixture(0700, 0600);
      const auto configPath = fixture.runtimeDirectory / "greeter.toml";
      std::ofstream(configPath) << R"toml(
[appearance]
scheme = "Synced"

[output]
use_synced_settings = false
name = "DP-2"
layout = "DP-1:10,20; DP-2:1800,20"
transforms = "DP-1:normal; DP-2:180"
scales = "DP-1:1.25; DP-2:1.5"
scale = 1.5
)toml";
      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      if (startup.existingSync) {
        expect(
            "stage conflicting synced output mappings",
            greeter::applyAppearanceSyncGreeterConf(
                std::string(kSyncedOutputLayout), std::string(kSyncedOutputTransforms),
                std::string(kSyncedOutputScales), std::nullopt
            ),
            true, startup.name, passed
        );
      }

      greeter_compositor_config config{};
      std::vector<greeter::GreeterOutputPlacement> placements;
      if (startup.compositorFirst) {
        greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
        placements = greeter::loadGreeterOutputLayout();
      } else {
        placements = greeter::loadGreeterOutputLayout();
        greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      }
      expectOutputMappings(
          startup.name, config, "DP-1:10,20; DP-2:1800,20", "DP-1:normal; DP-2:180", "DP-1:1.25; DP-2:1.5", passed
      );
      expect(
          "declarative output pin applies", std::string_view(config.preferred_output) == "DP-2", true, startup.name,
          passed
      );
      expect("declarative global scale applies", config.manual_scale == 1.5f, true, startup.name, passed);
      expect(
          "declarative client layout applies",
          placements.size() == 2 && placements[0].x == 10 && placements[0].y == 20 && placements[1].x == 1800, true,
          startup.name, passed
      );

      const auto preserved = greeter::config::loadConfig(configPath);
      expect(
          "output opt-out remains declarative", preserved.outputUseSyncedSettings == false, true, startup.name, passed
      );
      expect(
          "manual layout remains declarative", preserved.outputLayout == "DP-1:10,20; DP-2:1800,20", true, startup.name,
          passed
      );
      expect(
          "manual transforms remain declarative", preserved.outputTransforms == "DP-1:normal; DP-2:180", true,
          startup.name, passed
      );
      expect(
          "manual scales remain declarative", preserved.outputScales == "DP-1:1.25; DP-2:1.5", true, startup.name,
          passed
      );
    }

    {
      Fixture fixture(0700, 0600);
      const ScopedStateDirectory stateDirectory(fixture.runtimeDirectory);
      const std::string layout = "Dell Inc. DELL U2723QE ABC123:0,0; LG Electronics LG HDR 4K XYZ789:2560,0";
      const bool applied = greeter::applyAppearanceSyncGreeterConf(
          layout, "Dell Inc. DELL U2723QE ABC123:normal; LG Electronics LG HDR 4K XYZ789:90",
          "Dell Inc. DELL U2723QE ABC123:1.25; LG Electronics LG HDR 4K XYZ789:1.5", std::nullopt
      );
      expect("stable output mappings with spaces are accepted", applied, true, {}, passed);

      const auto placements = greeter::loadGreeterOutputLayout();
      expect("two stable layout entries parse", placements.size() == 2, true, {}, passed);
      expect(
          "first stable layout identifier is preserved",
          placements.size() == 2 && placements[0].name == "Dell Inc. DELL U2723QE ABC123", true, {}, passed
      );
      expect(
          "second stable layout coordinates parse",
          placements.size() == 2 && placements[1].x == 2560 && placements[1].y == 0, true, {}, passed
      );
    }

    {
      Fixture fixture(0700, 0600);
      std::ofstream(fixture.runtimeDirectory / "greeter.toml") << R"toml(
[output]
refresh_rate = "DP-1:120; HDMI-A-1:60"
)toml";

      greeter_compositor_config config{};
      greeter_compositor_config_load(fixture.runtimeDirectory.c_str(), &config);
      expect(
          "per-output refresh rates parse", std::string_view(config.output_refresh_rate_map) == "DP-1:120; HDMI-A-1:60",
          true, {}, passed
      );
      expect("refresh map is not a global rate", config.manual_mode_refresh_mhz == 0, true, {}, passed);
    }

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
