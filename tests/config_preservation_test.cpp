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
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
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
    require(config.layout->loginPresentation == greeter::LoginPresentation::Full, "login mode lost");
    const auto& clock = config.layout->components[1];
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
        !fallback.layout->components[1].anchor && fallback.layout->components[1].y == -32,
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

  void testDefaultClockProfiles() {
    using namespace greeter;
    Fixture fixture;
    const auto configPath = fixture.directory / "greeter.toml";
    const auto syncPath = fixture.directory / "sync.toml";
    config::GreeterConfigFile admin;
    config::GreeterSyncFile sync;
    const auto resolve = [&] {
      require(
          config::writeConfig(configPath, admin) && config::writeSync(syncPath, sync), "clock fixture write failed"
      );
      const auto before = readFile(configPath), state = readFile(syncPath);
      const auto profile = resolveGreeterWidgetProfile(loadGreeterPreferences());
      require(readFile(configPath) == before && readFile(syncPath) == state, "clock normalization wrote config");
      return profile;
    };
    auto profile = resolve();
    require(profile.instances.size() == 2 && profile.placeDefaultClocks, "builtin pair missing");
    require(
        profile.instances[0].id == kDefaultTimeWidget && profile.instances[1].id == kDefaultDateWidget,
        "builtin IDs changed"
    );
    require(
        std::get<double>(profile.instances[0].settings.at("font_size")) == 112
            && std::get<std::int64_t>(profile.instances[1].settings.at("font_weight")) == 500,
        "default typography lost"
    );
    sync.widgets = WidgetCollection{std::vector<WidgetInstance>{}};
    profile = resolve();
    require(profile.instances.empty() && !profile.placeDefaultClocks, "explicit empty collection reinserted clocks");
    admin.clockEnabled = true;
    profile = resolve();
    require(
        profile.instances.size() == 2 && profile.placeDefaultClocks, "administrator visibility did not require pair"
    );
    auto time = profile.instances.front();
    time.settings["font_size"] = 72.0;
    time.transform.rotation = .4;
    time.transform.flipX = true;
    time.placement = WidgetAnchorPlacement{WidgetAnchor::TopRight, -30, 20};
    auto date = profile.instances.back();
    auto extra = time;
    extra.id = "independent-clock";
    sync.widgets = WidgetCollection{std::vector{time, date, extra}};
    admin.clockEnabled = false;
    admin.clockTimeFormat = "%H";
    profile = resolve();
    require(
        !profile.instances[0].enabled && !profile.instances[1].enabled && profile.instances[2].enabled,
        "legacy visibility affected unrelated clock"
    );
    require(
        std::get<std::string>(profile.instances[0].settings.at("format")) == "%H"
            && std::get<double>(profile.instances[0].settings.at("font_size")) == 72
            && profile.instances[0].placement == time.placement,
        "partial format override replaced other settings/placement"
    );
    admin.clockEnabled.reset();
    admin.clockTimeFormat = "";
    admin.clockDateFontSize = 23;
    profile = resolve();
    require(
        !profile.instances[0].enabled
            && profile.instances[1].enabled
            && std::get<double>(profile.instances[1].settings.at("font_size")) == 23,
        "hidden time/date typography normalization failed"
    );
    admin.layout.emplace();
    admin.layout->components[1].rotation = 0.2;
    admin.layout->components[1].flipX = false;
    profile = resolve();
    require(
        profile.placeDefaultClocks
            && profile.instances[0].transform.rotation == 0
            && !profile.instances[0].transform.flipX,
        "administrator geometry was duplicated or defeated by Sync transforms"
    );
    for (const auto& collection :
         {WidgetCollection{}, WidgetCollection{std::vector<WidgetInstance>{}}, WidgetCollection{std::vector{extra}}}) {
      admin.widgets = collection;
      profile = resolve();
      if (!collection.instances)
        require(
            profile.instances.size() == 2
                && profile.instances[0].enabled
                && std::get<double>(profile.instances[1].settings.at("font_size")) == 19,
            "modern defaults did not own collection"
        );
      else
        require(profile.instances == *collection.instances, "legacy fields changed modern administrator widgets");
    }
    admin.widgets.reset();
    admin.clockTimeFormat.reset();
    admin.clockDateFormat = "";
    sync.widgets.reset();
    profile = resolve();
    require(profile.instances[0].enabled && !profile.instances[1].enabled, "date-only hiding failed");
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

  constexpr std::string_view kCoreLayout = R"toml([layout]
login_mode = 'auto'
[layout.login]
anchor = 'center-left'
width = 400
[layout.branding]
visible = false
anchor = 'bottom-right'
x = 0
y = -4
width = 96
output = 'DP-1'
rotation = 0.25
scale = 0.5
flip_x = false
flip_y = true
)toml";

  void requireCoreLayout(const std::optional<greeter::GreeterLayoutConfig>& layout) {
    require(layout && layout->loginPresentation == greeter::LoginPresentation::Auto, "explicit Auto mode lost");
    require(layout->components[0].anchor == "center-left" && layout->components[0].width == 400, "login layout lost");
    const auto& part = layout->components[2];
    require(
        part.visible == false
            && part.anchor == "bottom-right"
            && part.x == 0
            && part.y == -4
            && part.width == 96
            && part.output == "DP-1"
            && part.rotation == 0.25
            && part.scale == 0.5
            && part.flipX == false
            && part.flipY == true,
        "core component fields changed"
    );
  }

  void testCoreLayoutFiles() {
    for (const bool mutableFile : {false, true}) {
      Fixture fixture;
      const auto path = fixture.directory / (mutableFile ? "sync.toml" : "greeter.toml");
      for (const auto content : {std::string_view{}, std::string_view("[layout]\n"), kCoreLayout}) {
        writeFile(path, content);
        const auto config = greeter::config::loadConfig(path);
        const auto sync = greeter::config::loadSync(path);
        const bool present = !content.empty();
        require(config.layout.has_value() == present && sync.layout.has_value() == present, "layout presence changed");
        require(config.layoutValid && !hasDiagnostic(path), "valid layout rejected");
        require(
            mutableFile ? greeter::config::writeSync(path, sync) : greeter::config::writeConfig(path, config),
            "core layout write failed"
        );
        const auto saved = greeter::config::loadConfig(path);
        require(saved.layout.has_value() == present, "write materialized or removed layout");
        if (content == kCoreLayout)
          requireCoreLayout(saved.layout);
        else if (saved.layout)
          require(!saved.layout->loginPresentation, "empty layout materialized Auto mode");
      }
      for (const auto* mode : {"auto", "full", "minimal"}) {
        writeFile(path, std::string("[layout]\nlogin_mode='") + mode + "'\n");
        const auto original = greeter::config::loadConfig(path);
        require(
            mutableFile ? greeter::config::writeSync(path, greeter::config::loadSync(path))
                        : greeter::config::writeConfig(path, original),
            "explicit mode write failed"
        );
        require(
            greeter::config::loadConfig(path).layout->loginPresentation == original.layout->loginPresentation,
            "explicit mode changed on write"
        );
      }
      writeFile(path, kCoreLayout);
      auto config = greeter::config::loadConfig(path);
      auto sync = greeter::config::loadSync(path);
      config.layout->components[2].rotation = sync.layout->components[2].rotation = 7;
      config.layout->components[2].scale = sync.layout->components[2].scale = 4;
      require(
          mutableFile ? greeter::config::writeSync(path, sync) : greeter::config::writeConfig(path, config),
          "transform bounds write failed"
      );
      const auto normalized = greeter::config::loadConfig(path).layout->components[2];
      require(
          normalized.rotation == 7 - 2 * std::numbers::pi && normalized.scale == 4, "rotation normalization failed"
      );

      for (const auto* invalid :
           {"layout=1\n", "[layout]\nlogin_mode='invalid'\n", "[layout.login]\nvisible=false\n",
            "[layout.login]\noutput='DP-1'\n", "[layout.branding]\nscale=0.49\n", "[layout.branding]\nscale=4.01\n",
            "[layout.branding]\nscale=nan\n", "[layout.branding]\nscale=true\n", "[layout.branding]\nrotation=inf\n",
            "[layout.branding]\nrotation='0.5'\n", "[layout.branding]\nflip_x=1\n",
            "[layout.branding]\nflip_y='true'\n"}) {
        writeFile(path, invalid);
        const auto badConfig = greeter::config::loadConfig(path);
        const auto badSync = greeter::config::loadSync(path);
        require(!badConfig.layoutValid && !badSync.layout && hasDiagnostic(path), "invalid core layout accepted");
        require(
            !(mutableFile ? greeter::config::writeSync(path, sync) : greeter::config::writeConfig(path, config)),
            "writer replaced invalid layout source"
        );
        require(readFile(path) == invalid, "refused layout write changed bytes");
        writeFile(path, kCoreLayout);
        if (!mutableFile)
          require(!greeter::config::writeConfig(path, badConfig), "stale invalid administrator model was saved");
        (void)greeter::config::loadConfig(path);
        require(!hasDiagnostic(path), "layout diagnostic survived repair");
      }
      for (const auto invalid :
           {0.0, 4.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        writeFile(path, kCoreLayout);
        config = greeter::config::loadConfig(path);
        sync = greeter::config::loadSync(path);
        config.layout->components[2].scale = sync.layout->components[2].scale = invalid;
        require(
            !(mutableFile ? greeter::config::writeSync(path, sync) : greeter::config::writeConfig(path, config)),
            "writer accepted invalid in-memory scale"
        );
        require(readFile(path) == kCoreLayout, "invalid in-memory scale changed file");
      }
      writeFile(path, kCoreLayout);
      config = greeter::config::loadConfig(path);
      sync = greeter::config::loadSync(path);
      config.layout->components[2].rotation = sync.layout->components[2].rotation =
          std::numeric_limits<double>::infinity();
      require(
          !(mutableFile ? greeter::config::writeSync(path, sync) : greeter::config::writeConfig(path, config)),
          "writer accepted infinite rotation"
      );
      require(readFile(path) == kCoreLayout, "infinite in-memory rotation changed file");
    }
  }

  void testCoreLayoutPrecedence() {
    Fixture fixture;
    const auto config = fixture.directory / "greeter.toml";
    const auto sync = fixture.directory / "sync.toml";
    writeFile(sync, kCoreLayout);
    for (const auto* admin : {"", "[layout]\n", "[layout.branding]\n"}) {
      writeFile(config, admin);
      requireCoreLayout(greeter::loadGreeterPreferences().layout);
    }
    std::filesystem::remove(config);
    requireCoreLayout(greeter::loadGreeterPreferences().layout);
    writeFile(sync, R"([layout]
login_mode='full'
[layout.branding]
visible=false
anchor='bottom-right'
x=17
y=18
width=96
output='DP-2'
rotation=0.5
scale=2
flip_x=true
flip_y=false
)");
    writeFile(config, kCoreLayout);
    requireCoreLayout(greeter::loadGreeterPreferences().layout);
    writeFile(config, "[layout.branding]\nx=0\nflip_x=false\n");
    const auto partial = greeter::loadGreeterPreferences().layout;
    const auto& part = partial.components[2];
    require(
        partial.loginPresentation == greeter::LoginPresentation::Full
            && part.visible == false
            && part.anchor == "bottom-right"
            && part.x == 0
            && part.y == 18
            && part.width == 96
            && part.output == "DP-2"
            && part.rotation == 0.5
            && part.scale == 2
            && part.flipX == false
            && part.flipY == false,
        "per-field core layout priority failed"
    );

    writeFile(sync, R"([layout.clock]
visible=false
anchor='bottom-right'
[layout.branding]
visible=false
[layout.power]
visible=true
anchor='top-left'
[layout.appearance]
visible=false
anchor='bottom-left'
)");
    writeFile(config, R"([clock]
enabled=true
position='center-left'
[appearance]
hide_logo=false
power_buttons_position='hidden'
scheme_selector_position='top-right'
)");
    auto effective = greeter::effectiveGreeterLayout(greeter::loadGreeterPreferences());
    require(
        effective.components[1].visible == true
            && effective.components[1].anchor == "center-left"
            && effective.components[2].visible == true
            && effective.components[3].visible == false
            && effective.components[3].anchor == "top-left"
            && effective.components[4].anchor == "top-right"
            && effective.components[4].visible == false,
        "explicit legacy administrator keys lost to Sync"
    );
    writeFile(config, R"([clock]
enabled=false
position='above-panel'
[appearance]
hide_logo=true
power_buttons_position='top-right'
scheme_selector_position='hidden'
[layout.clock]
visible=true
anchor='top-center'
[layout.branding]
visible=true
[layout.power]
anchor='bottom-left'
[layout.appearance]
visible=true
)");
    effective = greeter::effectiveGreeterLayout(greeter::loadGreeterPreferences());
    require(
        effective.components[1].visible == true
            && effective.components[1].anchor == "top-center"
            && effective.components[2].visible == true
            && effective.components[3].anchor == "bottom-left"
            && effective.components[4].visible == true,
        "modern administrator fields lost to legacy keys"
    );
    writeFile(config, "[clock]\nposition='above-panel'\n");
    require(greeter::loadGreeterPreferences().layout.components[1].anchor == "above-panel", "legacy above-panel lost");
    writeFile(config, "");
    effective = greeter::effectiveGreeterLayout(greeter::loadGreeterPreferences());
    require(
        effective.components[1].visible == false
            && effective.components[1].anchor == "bottom-right"
            && effective.components[2].visible == false
            && effective.components[3].anchor == "top-left",
        "legacy defaults masked explicit synced settings"
    );
    std::filesystem::remove(sync);
    const auto defaults = greeter::loadGreeterPreferences().layout;
    require(!defaults.loginPresentation && !defaults.components[0].anchor, "missing layout created overrides");
  }

  void testCoreLayoutRecoveryAndSaves() {
    Fixture fixture;
    const auto config = fixture.directory / "greeter.toml";
    const auto target = fixture.directory / "declarative.toml";
    const auto sync = fixture.directory / "sync.toml";
    writeFile(sync, kCoreLayout);
    for (const bool symlink : {false, true}) {
      for (const auto* invalid : {"[layout\n", "[layout.branding]\nscale=0\nx=12\n", "layout=1\n"}) {
        std::filesystem::remove(config);
        writeFile(symlink ? target : config, invalid);
        if (symlink)
          std::filesystem::create_symlink(target, config);
        const auto blocked = greeter::loadGreeterPreferences().layout;
        require(
            !blocked.loginPresentation && !blocked.components[0].anchor && !blocked.components[2].scale,
            "invalid administrator intent inherited Sync layout"
        );
        if (std::string_view(invalid).contains("x=12"))
          require(blocked.components[2].x == 12, "valid administrator sibling was discarded");
        require(hasDiagnostic(config) && readFile(config) == invalid, "resolution changed invalid administrator file");
        writeFile(symlink ? target : config, "[layout]\n");
        requireCoreLayout(greeter::loadGreeterPreferences().layout);
        require(!hasDiagnostic(config), "repaired layout stayed blocked");
      }
    }
    std::filesystem::remove(config);
    writeFile(config, "[widgets]\nwidget_order='invalid'\n");
    requireCoreLayout(greeter::loadGreeterPreferences().layout);
    require(hasDiagnostic(config), "unrelated widget diagnostic disappeared");
    writeFile(config, kConfig);
    writeFile(sync, std::string(kSync) + std::string(kCoreLayout));
    require(greeter::config::writeSync(sync, greeter::config::loadSync(sync)), "canonical Sync fixture write failed");
    const auto before = *toml::parse_file(sync.string())["layout"].as_table();
    auto prefs = greeter::loadGreeterPreferences();
    prefs.layout = {};
    prefs.session = "New Session";
    require(greeter::saveGreeterPreferences(prefs), "preference save lost layout");
    requireCoreLayout(greeter::config::loadSync(sync).layout);
    require(*toml::parse_file(sync.string())["layout"].as_table() == before, "UI persisted effective layout");
    greeter::GreeterSyncAppearanceUpdate update;
    update.appearance.fontFamily = "Updated Font";
    require(greeter::applyAppearanceSyncGreeterConf({}, {}, {}, update), "appearance update failed with layout");
    requireCoreLayout(greeter::config::loadSync(sync).layout);
    requireWidgets(greeter::config::loadSync(sync).widgets);
    require(readFile(config) == kConfig, "mutable saves changed administrator layout");
    writeFile(sync, "[layout.branding]\nscale=0\nx=12\n");
    const auto admin = greeter::loadGreeterPreferences().layout;
    require(
        admin.loginPresentation == greeter::LoginPresentation::Full && !admin.components[2].x,
        "invalid Sync leaked partial layout or replaced valid administrator mode"
    );
    require(hasDiagnostic(sync), "invalid lower-priority layout diagnostic hidden");
    std::filesystem::remove(config);
    require(!greeter::loadGreeterPreferences().layout.loginPresentation, "invalid Sync created layout overrides");
    writeFile(sync, kCoreLayout);
    requireCoreLayout(greeter::loadGreeterPreferences().layout);
    require(!hasDiagnostic(sync), "repaired Sync diagnostic persisted");
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
        testWidgetUpdates, testWidgetPreferences, testDefaultClockProfiles, testWidgetPreferenceRecovery,
        testCoreLayoutFiles, testCoreLayoutPrecedence, testCoreLayoutRecoveryAndSaves}) {
    try {
      test();
    } catch (const std::exception& error) {
      std::fprintf(stderr, "config preservation: %s\n", error.what());
      passed = false;
    }
  }
  return passed ? 0 : 1;
}
