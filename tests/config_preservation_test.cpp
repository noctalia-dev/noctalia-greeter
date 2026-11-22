#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"

#include <algorithm>
#include <array>
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
#include <toml++/toml.hpp>
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
[widgets]
widget_order = ['caption']
[widgets.widget.caption]
type = 'label'
placement = 'anchor'
anchor = 'center-left'
rotation = -0.25
flip_x = true
[widgets.widget.caption.settings]
title = 'Saved widget'
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
[widgets]
widget_order = ['caption']
[widgets.widget.caption]
type = 'label'
placement = 'anchor'
anchor = 'center-left'
rotation = -0.25
flip_x = true
[widgets.widget.caption.settings]
title = 'Saved widget'
)toml";

  void requireWidgets(const std::optional<greeter::WidgetCollection>& widgets) {
    require(widgets && widgets->instances && widgets->instances->size() == 1, "widget collection lost");
    const auto& widget = widgets->instances->front();
    require(
        widget.id == "caption"
            && widget.type == "label"
            && widget.transform.rotation == -0.25
            && widget.transform.flipX
            && std::get<std::string>(widget.settings.at("title")) == "Saved widget",
        "stored widget data changed"
    );
  }

  void requireSettings(const greeter::config::GreeterConfigFile& config) {
    requireWidgets(config.widgets);
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
    requireWidgets(saved.widgets);
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
    requireWidgets(migrated.widgets);
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

  void testWidgetFiles() {
    for (const bool mutableFile : {false, true}) {
      Fixture fixture;
      const auto path = fixture.directory / (mutableFile ? "sync.toml" : "greeter.toml");
      for (const auto content :
           {std::string_view{}, std::string_view("[widgets]\n"), std::string_view("[widgets]\nwidget_order=[]\n"),
            mutableFile ? kSync : kConfig}) {
        writeFile(path, content);
        const auto before =
            mutableFile ? greeter::config::loadSync(path).widgets : greeter::config::loadConfig(path).widgets;
        require(!hasDiagnostic(path), "valid widget file diagnosed");
        const bool written = mutableFile ? greeter::config::writeSync(path, greeter::config::loadSync(path))
                                         : greeter::config::writeConfig(path, greeter::config::loadConfig(path));
        require(written, "widget file roundtrip failed");
        const auto after =
            mutableFile ? greeter::config::loadSync(path).widgets : greeter::config::loadConfig(path).widgets;
        require(after == before, "file write changed widget collection intent/data");
      }
      for (const auto content :
           {std::string_view("[widgets\n"), std::string_view("widgets = false\n"),
            std::string_view("[widgets]\nwidget_order = 'invalid'\n"),
            std::string_view("[widgets]\nwidget_order = ['missing']\n")}) {
        writeFile(path, content);
        require(::chmod(path.c_str(), 0640) == 0, "fixture chmod failed");
        const bool written =
            mutableFile ? greeter::config::writeSync(path, {}) : greeter::config::writeConfig(path, {});
        require(!written && hasDiagnostic(path), "writer accepted invalid widget file");
        struct stat metadata{};
        require(
            ::stat(path.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0640, "refused write changed mode"
        );
        require(readFile(path) == content, "refused widget write changed file bytes");
      }
      writeFile(path, mutableFile ? kSync : kConfig);
      if (mutableFile) {
        auto model = greeter::config::loadSync(path);
        model.widgets->instances->front().settings["title"] = std::int64_t{42};
        require(!greeter::config::writeSync(path, model), "invalid widget model written");
      } else {
        auto model = greeter::config::loadConfig(path);
        model.widgets->instances->front().settings["title"] = std::int64_t{42};
        require(!greeter::config::writeConfig(path, model), "invalid widget model written");
      }
      require(readFile(path) == (mutableFile ? kSync : kConfig), "invalid model changed existing file");
      const auto diagnostic = std::ranges::find_if(greeter::config::configDiagnostics(), [&](const auto& item) {
        return item.path == path && item.message.find("widgets.widget.caption.settings.title") != std::string::npos;
      });
      require(diagnostic != greeter::config::configDiagnostics().end(), "widget field diagnostic missing");
      const bool repaired = mutableFile ? greeter::config::writeSync(path, greeter::config::loadSync(path))
                                        : greeter::config::writeConfig(path, greeter::config::loadConfig(path));
      require(repaired && !hasDiagnostic(path), "widget diagnostic did not recover after repair");
    }
  }

  void testWidgetUpdates() {
    Fixture fixture;
    const auto config = fixture.directory / "greeter.toml";
    const auto sync = fixture.directory / "sync.toml";
    writeFile(config, kConfig);
    writeFile(sync, kSync);
    greeter::GreeterSyncAppearanceUpdate update;
    update.replaceSession = false;
    update.appearance.fontFamily = "Updated Sans";
    require(
        greeter::applyAppearanceSyncGreeterConf(std::nullopt, std::nullopt, std::nullopt, update),
        "appearance update failed"
    );
    const auto updated = greeter::config::loadSync(sync);
    requireWidgets(updated.widgets);
    require(
        updated.sessionLast == "Old Session"
            && updated.sessionPowerSuspend == "test-suspend"
            && updated.sessionActions.size() == 1
            && updated.appearance.fontFamily == "Updated Sans",
        "appearance update changed unrelated state or failed to apply"
    );
    require(readFile(config) == kConfig, "appearance update rewrote administrator file");

    const auto invalid = std::string(kSync) + "\nunknown_widget_setting = true\n";
    writeFile(sync, invalid);
    greeter::GreeterPreferences preferences;
    preferences.session = "Other Session";
    require(!greeter::saveGreeterPreferences(preferences), "preferences overwrote invalid widget settings");
    require(
        !greeter::applyAppearanceSyncGreeterConf(std::nullopt, std::nullopt, std::nullopt, update),
        "appearance update overwrote invalid widget settings"
    );
    require(readFile(sync) == invalid, "invalid widget settings were rewritten");
    const auto diagnostic = std::ranges::find_if(greeter::config::configDiagnostics(), [&](const auto& item) {
      return item.path == sync
          && item.line > 0
          && item.column > 0
          && item.message.find("widgets.widget.caption.settings.unknown_widget_setting") != std::string::npos;
    });
    require(diagnostic != greeter::config::configDiagnostics().end(), "widget source location missing");
    // An error retained for sync must not block an independent declarative write.
    const auto model = greeter::config::loadConfig(config);
    require(greeter::config::writeConfig(config, model) && hasDiagnostic(sync), "writer mixed file diagnostics");
  }

  void testWidgetPreferences() {
    using namespace greeter;
    WidgetInstance shared;
    shared.id = "shared";
    shared.type = "label";
    shared.settings = {{"title", std::string("Administrator")}};
    auto adminOnly = shared;
    adminOnly.id = "admin-only";
    const WidgetCollection administrator{std::vector{shared, adminOnly}};
    shared.settings["title"] = std::string("Sync");
    auto syncOnly = shared;
    syncOnly.id = "sync-only";
    const WidgetCollection synced{std::vector{syncOnly, shared}};
    // Unmanaged, defaults, empty, administrator custom, Sync custom.
    const std::array<std::optional<WidgetCollection>, 5> choices{
        std::nullopt, WidgetCollection{}, WidgetCollection{std::vector<WidgetInstance>{}}, administrator, synced
    };
    const std::array expected{
        std::array{0, 1, 2, 4}, std::array{1, 1, 1, 1}, std::array{2, 2, 2, 2}, std::array{3, 3, 3, 3}
    };
    for (std::size_t admin = 0; admin < 4; ++admin) {
      for (std::size_t sync = 0; sync < 4; ++sync) {
        Fixture fixture;
        const auto configPath = fixture.directory / "greeter.toml";
        const auto syncPath = fixture.directory / "sync.toml";
        config::GreeterConfigFile config;
        config.widgets = choices[admin];
        config.sessionDefault = "Pinned session";
        config.userDefault = "Pinned user";
        config.appearanceScheme = "Pinned scheme";
        config::GreeterSyncFile state;
        state.widgets = choices[sync == 3 ? 4 : sync];
        state.sessionLast = "Last session";
        state.appearanceScheme = "Sync scheme";
        require(config::writeConfig(configPath, config) && config::writeSync(syncPath, state), "fixture save failed");
        const auto adminBytes = readFile(configPath);
        const auto syncBytes = readFile(syncPath);
        auto prefs = loadGreeterPreferences();
        require(
            prefs.widgets == choices[static_cast<std::size_t>(expected[admin][sync])],
            "widget precedence/intent changed"
        );
        require(
            prefs.defaultSession == config.sessionDefault
                && prefs.defaultUser == config.userDefault
                && prefs.scheme == config.appearanceScheme
                && prefs.session == state.sessionLast,
            "widget resolution changed ordinary preferences"
        );
        require(
            readFile(configPath) == adminBytes && readFile(syncPath) == syncBytes, "preference load wrote profiles"
        );
        prefs.widgets = WidgetCollection{std::vector<WidgetInstance>{}};
        prefs.session = "New session";
        require(saveGreeterPreferences(prefs), "preference save failed");
        const auto saved = config::loadSync(syncPath);
        require(
            saved.widgets == state.widgets && saved.sessionLast == prefs.session,
            "preference save persisted effective widgets"
        );
        require(readFile(configPath) == adminBytes, "preference save rewrote administrator widgets");
      }
    }
  }

  void testWidgetPreferenceRecovery() {
    Fixture fixture;
    const auto configPath = fixture.directory / "greeter.toml";
    const auto syncPath = fixture.directory / "sync.toml";
    writeFile(syncPath, kSync);
    auto state = greeter::config::loadSync(syncPath);
    state.widgets->instances->front().id = "synced-caption";
    require(greeter::config::writeSync(syncPath, state), "sync fixture save failed");
    for (const bool symlink : {false, true}) {
      for (const auto invalid :
           {std::string_view("widgets=false\n"), std::string_view("[widgets]\nwidget_order='invalid'\n"),
            std::string_view("[widgets\n")}) {
        const auto target = symlink ? fixture.directory / "declarative.toml" : configPath;
        writeFile(target, invalid);
        if (symlink)
          std::filesystem::create_symlink(target, configPath);
        require(
            !greeter::loadGreeterPreferences().widgets && hasDiagnostic(configPath),
            "invalid admin inherited Sync widgets"
        );
        const auto stale = greeter::config::loadConfig(configPath);
        require(!stale.widgetsValid, "invalid administrator collection lost its validity state");
        writeFile(target, kConfig);
        require(!greeter::config::writeConfig(configPath, stale), "stale invalid model cleared repaired widgets");
        require(readFile(target) == kConfig, "stale invalid write changed repaired source");
        requireWidgets(greeter::loadGreeterPreferences().widgets);
        require(!hasDiagnostic(configPath), "repaired administrator widget error persisted");
        std::filesystem::remove(configPath);
        require(
            greeter::loadGreeterPreferences().widgets == state.widgets, "removing admin override did not reveal Sync"
        );
        if (symlink)
          std::filesystem::remove(target);
      }
    }
    writeFile(configPath, "[layout.clock]\nanchor='invalid'\n");
    require(
        greeter::loadGreeterPreferences().widgets == state.widgets && hasDiagnostic(configPath),
        "unrelated layout diagnostic blocked Sync widgets"
    );
    writeFile(configPath, kConfig);
    writeFile(syncPath, "[widgets]\nwidget_order='invalid'\n");
    requireWidgets(greeter::loadGreeterPreferences().widgets);
    require(hasDiagnostic(syncPath), "lower-priority diagnostic was hidden");
    std::filesystem::remove(configPath);
    require(!greeter::loadGreeterPreferences().widgets && hasDiagnostic(syncPath), "invalid Sync became a collection");
    require(greeter::config::loadConfig(configPath).widgetsValid, "missing administrator file treated as invalid");
    writeFile(syncPath, kSync);
    requireWidgets(greeter::loadGreeterPreferences().widgets);
    require(!hasDiagnostic(syncPath), "repaired Sync widget error persisted");
    std::filesystem::remove(syncPath);
    require(
        !greeter::loadGreeterPreferences().widgets && !hasDiagnostic(syncPath), "missing sources materialized widgets"
    );
    require(
        !std::filesystem::exists(configPath) && !std::filesystem::exists(syncPath),
        "widget resolution created config files"
    );
  }

  void testSetup() {
    require(::geteuid() == 0, "setup test requires root in a private user namespace");
    for (const bool symlink : {false, true}) {
      for (const auto content :
           {kConfig, std::string_view("[session\n"), std::string_view("[layout.clock]\nanchor='invalid'\n"),
            std::string_view("[layout.unknown]\nvisible=true\n"),
            std::string_view("[widgets]\nwidget_order='invalid'\n")}) {
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
  for (const auto test :
       {testConfigWriter, testPreferenceSave, testLegacyAppearance, testDiagnosticRecovery, testWidgetFiles,
        testWidgetUpdates, testWidgetPreferences, testWidgetPreferenceRecovery}) {
    try {
      test();
    } catch (const std::exception& error) {
      std::fprintf(stderr, "config preservation: %s\n", error.what());
      passed = false;
    }
  }
  return passed ? 0 : 1;
}
