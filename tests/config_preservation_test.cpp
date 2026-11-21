#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  void require(bool condition, std::string_view message) {
    if (!condition) {
      throw std::runtime_error(std::string(message));
    }
  }

  void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path);
    file << content;
    file.close();
    require(!file.fail(), "fixture write failed");
  }

  std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    require(file.is_open(), "fixture read failed");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }

  bool hasDiagnostic(const std::filesystem::path& path) {
    return std::ranges::any_of(greeter::config::configDiagnostics(), [&](const auto& d) { return d.path == path; });
  }

  class Fixture {
  public:
    Fixture() {
      char pattern[] = "/tmp/noctalia-config-preservation-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      require(created != nullptr, "mkdtemp failed");
      directory = created;
      if (const char* current = std::getenv("NOCTALIA_GREETER_STATE_DIR")) {
        previousState = current;
      }
      require(::setenv("NOCTALIA_GREETER_STATE_DIR", directory.c_str(), 1) == 0, "setenv failed");
      greeter::config::clearConfigDiagnostics();
    }

    ~Fixture() {
      if (previousState) {
        ::setenv("NOCTALIA_GREETER_STATE_DIR", previousState->c_str(), 1);
      } else {
        ::unsetenv("NOCTALIA_GREETER_STATE_DIR");
      }
      greeter::config::clearConfigDiagnostics();
      std::error_code ec;
      std::filesystem::remove_all(directory, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::filesystem::path directory;

  private:
    std::optional<std::string> previousState;
  };

  constexpr std::string_view kConfig = R"toml([session]
default = "Niri"
[user]
default = "test-user"
[keyboard]
layout = "de"
[layout]
login_mode = "full"
[layout.clock]
anchor = "bottom-left"
x = 24
visible = false
)toml";

  constexpr std::string_view kSync = R"toml([session]
last = "Old Session"
[session.power]
suspend = "test-suspend"
[[session.actions]]
action = "reboot"
command = "test-reboot"
label = "Test restart"
[output]
layout = "DP-1:0,0; DP-2:1280,0"
transforms = "DP-1:90"
scales = "DP-1:2"
[appearance]
scheme = "Noctalia"
font_family = "Test Sans"
[appearance.palette]
primary = "#123456"
[appearance.wallpaper]
path = "color:#102030"
)toml";

  void requireSettings(const greeter::config::GreeterConfigFile& config) {
    require(config.sessionDefault == "Niri" && config.userDefault == "test-user", "configured defaults lost");
    require(config.keyboardLayout == "de", "keyboard setting lost");
    require(config.layout.loginPresentation == greeter::LoginPresentation::Full, "login mode lost");
    const auto& clock = config.layout.components[1];
    require(clock.anchor == "bottom-left" && clock.x == 24 && clock.visible == false, "clock overrides lost");
  }

  void testConfigWriter() {
    Fixture fixture;
    const auto target = fixture.directory / "declarative.toml";
    const auto path = fixture.directory / "greeter.toml";
    writeFile(target, kConfig);
    std::filesystem::create_symlink(target, path);
    const auto config = greeter::config::loadConfig(path);
    requireSettings(config);
    require(!greeter::config::writeConfig(path, config), "writer followed config symlink");
    require(std::filesystem::is_symlink(path) && readFile(target) == kConfig, "symlink write changed target");
    std::filesystem::remove(path);
    require(greeter::config::writeConfig(path, config), "ordinary config write failed");
    requireSettings(greeter::config::loadConfig(path));
    std::filesystem::remove(path);
    std::filesystem::remove(target);
    std::filesystem::create_symlink(target, path);
    require(!greeter::config::writeConfig(path, config), "writer followed dangling config symlink");
    require(!std::filesystem::exists(target), "writer created dangling symlink target");
  }

  void testPreferenceSave() {
    Fixture fixture;
    const auto config = fixture.directory / "greeter.toml";
    const auto sync = fixture.directory / "sync.toml";
    writeFile(config, kConfig);
    greeter::GreeterPreferences prefs;
    prefs.session = "New Session";
    prefs.scheme = "Noctalia";
    require(greeter::saveGreeterPreferences(prefs), "save with missing sync failed");
    require(greeter::config::loadSync(sync).sessionLast == prefs.session, "first session choice not saved");
    const std::string invalid = std::string(kSync) + "\nbroken = [\n";
    writeFile(sync, invalid);
    for (const auto* scheme : {"Noctalia", "Synced"}) {
      prefs.scheme = scheme;
      require(!greeter::saveGreeterPreferences(prefs), "saved over invalid sync");
      require(readFile(sync) == invalid && readFile(config) == kConfig, "refused save changed file bytes");
      require(hasDiagnostic(sync), "invalid sync diagnostic missing");
    }
    // Repair in the same process without clearing diagnostics on the caller's behalf.
    writeFile(sync, kSync);
    require(greeter::saveGreeterPreferences(prefs), "save did not recover after sync repair");
    require(!hasDiagnostic(sync), "repaired sync still has a diagnostic");
    const auto saved = greeter::config::loadSync(sync);
    require(saved.sessionLast == prefs.session && saved.appearanceScheme == prefs.scheme, "new choices not saved");
    require(
        saved.outputLayout == "DP-1:0,0; DP-2:1280,0"
            && saved.outputTransforms == "DP-1:90"
            && saved.outputScales == "DP-1:2",
        "UI save changed output mappings"
    );
    require(
        saved.sessionPowerSuspend == "test-suspend"
            && saved.sessionActions.size() == 1
            && saved.sessionActions[0].command == "test-reboot",
        "UI save changed power actions"
    );
    require(
        saved.appearance.fontFamily == "Test Sans"
            && saved.appearance.palette.at("primary") == "#123456"
            && saved.appearance.wallpaper
            && saved.appearance.wallpaper->path == "color:#102030",
        "UI save changed synced appearance"
    );
    require(readFile(config) == kConfig, "UI save rewrote administrator config");
  }

  void testLegacyAppearance() {
    Fixture fixture;
    const auto sync = fixture.directory / "sync.toml";
    nlohmann::json manifest = {{"version", greeter::appearance::kManifestVersion}};
    for (const auto key : greeter::appearance::requiredPaletteKeys()) {
      manifest["palette"][std::string(key)] = "#123456";
    }
    const auto jsonPath = fixture.directory / "appearance.json";
    const auto json = manifest.dump();
    writeFile(jsonPath, json);
    constexpr std::string_view invalid = "[output]\nlayout = [\n";
    writeFile(sync, invalid);
    require(loadGreeterSyncedAppearance().has_value(), "invalid sync prevented legacy appearance fallback");
    require(readFile(sync) == invalid && readFile(jsonPath) == json, "migration changed invalid sync or source JSON");
    writeFile(sync, kSync);
    require(loadGreeterSyncedAppearance().has_value(), "legacy appearance did not recover after repair");
    const auto migrated = greeter::config::loadSync(sync);
    require(migrated.appearance.hasCompletePalette(), "legacy migration blocked by stale diagnostic");
    require(
        migrated.sessionLast == "Old Session"
            && migrated.sessionPowerSuspend == "test-suspend"
            && migrated.sessionActions.size() == 1
            && migrated.outputScales == "DP-1:2",
        "migration changed unrelated mutable settings"
    );
    std::filesystem::remove(sync);
    require(
        loadGreeterSyncedAppearance().has_value() && greeter::config::loadSync(sync).appearance.hasCompletePalette(),
        "migration into missing sync failed"
    );
  }

  void testDiagnosticRecovery() {
    Fixture fixture;
    const auto config = fixture.directory / "greeter.toml";
    const auto sync = fixture.directory / "sync.toml";
    writeFile(config, "[layout.clock]\nanchor='invalid'\ny=-32\n");
    writeFile(sync, "[session\n");
    const auto fallback = greeter::config::loadConfig(config);
    (void)greeter::config::loadSync(sync);
    require(
        !fallback.layout.components[1].anchor && fallback.layout.components[1].y == -32,
        "runtime lost valid sibling field during fallback"
    );
    require(hasDiagnostic(config) && hasDiagnostic(sync), "fixture diagnostics missing");
    writeFile(sync, kSync);
    (void)greeter::config::loadSync(sync);
    require(!hasDiagnostic(sync) && hasDiagnostic(config), "sync reload did not refresh only its own diagnostics");
    writeFile(config, kConfig);
    requireSettings(greeter::config::loadConfig(config));
    require(!hasDiagnostic(config), "repaired administrator config still has a diagnostic");
    writeFile(sync, "[session\n");
    (void)greeter::config::loadSync(sync);
    std::filesystem::remove(sync);
    (void)greeter::config::loadSync(sync);
    require(!hasDiagnostic(sync), "removed sync kept a stale diagnostic");
  }

  void testSetup() {
    require(::geteuid() == 0, "setup test requires root in a private user namespace");
    for (const bool symlink : {false, true}) {
      for (const auto content :
           {kConfig, std::string_view("[session\n"), std::string_view("[layout.clock]\nanchor='invalid'\n"),
            std::string_view("[layout.unknown]\nvisible=true\n")}) {
        Fixture fixture;
        const auto config = fixture.directory / "greeter.toml";
        const auto target = symlink ? fixture.directory / "declarative.toml" : config;
        writeFile(target, content);
        if (symlink) {
          require(::chmod(target.c_str(), 0444) == 0, "fixture chmod failed");
          std::filesystem::create_symlink(target, config);
        }
        std::string error;
        const bool valid = content == kConfig;
        require(greeter::installGreeterSystemLayout("root", error) == valid, "unexpected setup result: " + error);
        if (valid) {
          require(!std::filesystem::is_symlink(config), "setup did not convert config symlink");
          requireSettings(greeter::config::loadConfig(config));
          require(std::filesystem::is_regular_file(fixture.directory / "sync.toml"), "setup did not create sync");
        } else {
          require(
              error.find("refusing to overwrite invalid greeter.toml") != std::string::npos,
              "setup refusal lacks useful error"
          );
          require(readFile(config) == content, "setup rewrote invalid config");
          require(std::filesystem::is_symlink(config) == symlink, "setup removed invalid config symlink");
        }
        if (symlink) {
          require(readFile(target) == content, "setup changed declarative target");
          require(
              (std::filesystem::status(target).permissions() & std::filesystem::perms::owner_write)
                  == std::filesystem::perms::none,
              "setup changed declarative target permissions"
          );
        }
      }
    }
    Fixture fixture;
    std::string error;
    require(greeter::installGreeterSystemLayout("root", error), "setup with missing config failed: " + error);
    require(std::filesystem::is_regular_file(fixture.directory / "greeter.toml"), "setup did not create config");
  }
} // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--setup") {
    try {
      testSetup();
      std::puts("setup config preservation passed");
      return 0;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "setup config preservation: %s\n", error.what());
      return 1;
    }
  }
  bool passed = true;
  for (const auto test : {testConfigWriter, testPreferenceSave, testLegacyAppearance, testDiagnosticRecovery}) {
    try {
      test();
    } catch (const std::exception& error) {
      std::fprintf(stderr, "config preservation: %s\n", error.what());
      passed = false;
    }
  }
  return passed ? 0 : 1;
}
