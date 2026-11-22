#include "greeter/greeter_preferences.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/privileged_state_paths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <pwd.h>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

  constexpr Logger kLog("greeter-prefs");

  constexpr mode_t kSyncedDirMode = S_IRWXU | S_IRGRP | S_IXGRP;
  constexpr mode_t kGreeterConfMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  bool validLegacyClockPosition(std::string_view position) {
    constexpr std::array<std::string_view, 8> positions{"above-panel", "top-left",      "top-center",   "top-right",
                                                        "bottom-left", "bottom-center", "bottom-right", "center-left"};
    return std::ranges::find(positions, position) != positions.end();
  }

  void mapLegacyCorner(greeter::ComponentPlacement& part, const std::optional<std::string>& legacy) {
    if (!legacy)
      return;
    if (*legacy == "hidden") {
      if (!part.visible)
        part.visible = false;
    } else if (!part.anchor && greeter::validLayoutAnchor(*legacy))
      part.anchor = *legacy;
  }

  greeter::GreeterLayoutConfig
  overlayLayout(greeter::GreeterLayoutConfig base, const greeter::GreeterLayoutConfig& preferred) {
    base.outputs.clear();
    if (preferred.loginPresentation)
      base.loginPresentation = preferred.loginPresentation;
    for (std::size_t i = 0; i < base.components.size(); ++i) {
      auto& target = base.components[i];
      const auto& source = preferred.components[i];
      const auto prefer = [](auto& destination, const auto& value) {
        if (value)
          destination = value;
      };
      prefer(target.visible, source.visible);
      prefer(target.anchor, source.anchor);
      prefer(target.x, source.x);
      prefer(target.y, source.y);
      prefer(target.width, source.width);
      prefer(target.output, source.output);
      prefer(target.rotation, source.rotation);
      prefer(target.scale, source.scale);
      prefer(target.flipX, source.flipX);
      prefer(target.flipY, source.flipY);
    }
    return base;
  }
  greeter::GreeterLayoutConfig
  resolveStoredLayout(const greeter::config::GreeterConfigFile& file, const greeter::config::GreeterSyncFile& sync) {
    auto result = file.layout.value_or(greeter::GreeterLayoutConfig{});
    // Explicit legacy administrator keys also outrank Sync; their defaults do not.
    auto& clock = result.components[1];
    if (!clock.visible && file.clockEnabled)
      clock.visible = file.clockEnabled;
    if (!clock.anchor && file.clockPosition && validLegacyClockPosition(*file.clockPosition))
      clock.anchor = file.clockPosition;
    auto& branding = result.components[2];
    if (!branding.visible && file.appearanceHideLogo)
      branding.visible = !*file.appearanceHideLogo;
    mapLegacyCorner(result.components[3], file.appearancePowerButtonsPosition);
    mapLegacyCorner(result.components[4], file.appearanceSchemeSelectorPosition);

    const auto admin = result;
    const auto synced =
        file.layoutValid ? sync.layout.value_or(greeter::GreeterLayoutConfig{}) : greeter::GreeterLayoutConfig{};
    result = overlayLayout(synced, admin);
    std::set<std::string> outputs;
    for (const auto& [name, unused] : synced.outputs)
      outputs.insert(name);
    for (const auto& [name, unused] : admin.outputs)
      outputs.insert(name);
    for (const auto& name : outputs) {
      auto localSync = synced;
      localSync.outputs.clear();
      if (const auto local = synced.outputs.find(name); local != synced.outputs.end())
        localSync = overlayLayout(localSync, local->second);
      auto localAdmin = admin;
      localAdmin.outputs.clear();
      if (const auto local = admin.outputs.find(name); local != admin.outputs.end())
        localAdmin = overlayLayout(localAdmin, local->second);
      result.outputs.emplace(name, overlayLayout(localSync, localAdmin));
    }
    return result;
  }

  [[nodiscard]] std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
      ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
      --end;
    }
    return std::string(value.substr(begin, end - begin));
  }

  template <typename Callback> void forEachOutputMappingEntry(std::string_view raw, Callback&& callback) {
    const bool semicolonSeparated = raw.find(';') != std::string_view::npos;
    std::size_t begin = 0;
    while (begin < raw.size()) {
      while (
          begin < raw.size()
          && (std::isspace(static_cast<unsigned char>(raw[begin])) != 0 || (semicolonSeparated && raw[begin] == ';'))) {
        ++begin;
      }
      if (begin >= raw.size()) {
        break;
      }

      std::size_t end = begin;
      if (semicolonSeparated) {
        while (end < raw.size() && raw[end] != ';') {
          ++end;
        }
      } else {
        while (end < raw.size() && std::isspace(static_cast<unsigned char>(raw[end])) == 0) {
          ++end;
        }
      }
      callback(raw.substr(begin, end - begin));
      begin = end;
    }
  }

  [[nodiscard]] std::optional<greeter::GreeterOutputPlacement> parseOutputLayoutEntry(std::string_view token) {
    const std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return std::nullopt;
    }

    const std::size_t colon = trimmed.rfind(':');
    if (colon == std::string_view::npos || colon == 0) {
      return std::nullopt;
    }

    const std::string name = trim(trimmed.substr(0, colon));
    const std::string coords = trim(trimmed.substr(colon + 1));
    if (name.empty() || coords.empty()) {
      return std::nullopt;
    }

    const std::size_t comma = coords.find(',');
    if (comma == std::string_view::npos) {
      return std::nullopt;
    }

    const std::string xRaw = trim(coords.substr(0, comma));
    const std::string yRaw = trim(coords.substr(comma + 1));
    if (xRaw.empty() || yRaw.empty()) {
      return std::nullopt;
    }

    auto parseCoord = [](const std::string& raw) -> std::optional<int32_t> {
      char* end = nullptr;
      errno = 0;
      const long value = std::strtol(raw.c_str(), &end, 10);
      if (errno != 0 || end == raw.c_str() || *end != '\0') {
        return std::nullopt;
      }
      if (value < INT32_MIN || value > INT32_MAX) {
        return std::nullopt;
      }
      return static_cast<int32_t>(value);
    };

    const auto x = parseCoord(xRaw);
    const auto y = parseCoord(yRaw);
    if (!x.has_value() || !y.has_value()) {
      return std::nullopt;
    }

    greeter::GreeterOutputPlacement placement;
    placement.name = name;
    placement.x = *x;
    placement.y = *y;
    return placement;
  }

  [[nodiscard]] std::vector<greeter::GreeterOutputPlacement> parseOutputLayoutValue(std::string_view raw) {
    std::vector<greeter::GreeterOutputPlacement> placements;
    forEachOutputMappingEntry(raw, [&placements](const std::string_view entry) {
      if (const auto placement = parseOutputLayoutEntry(entry)) {
        placements.push_back(*placement);
      } else {
        kLog.warn("ignoring invalid output layout entry '{}'", entry);
      }
    });

    return placements;
  }

  [[nodiscard]] bool isValidOutputTransformToken(std::string_view token) {
    return token == "normal"
        || token == "0"
        || token == "none"
        || token == "90"
        || token == "180"
        || token == "270"
        || token == "flipped"
        || token == "flipped-90"
        || token == "flipped_90"
        || token == "flipped-180"
        || token == "flipped_180"
        || token == "flipped-270"
        || token == "flipped_270";
  }

  [[nodiscard]] bool parseOutputTransformEntry(std::string_view token) {
    const std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return false;
    }

    const std::size_t colon = trimmed.rfind(':');
    if (colon == std::string_view::npos || colon == 0) {
      return false;
    }

    const std::string name = trim(trimmed.substr(0, colon));
    const std::string value = trim(trimmed.substr(colon + 1));
    return !name.empty() && isValidOutputTransformToken(value);
  }

  [[nodiscard]] bool parseOutputScaleEntry(std::string_view token) {
    const std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return false;
    }

    const std::size_t colon = trimmed.rfind(':');
    if (colon == std::string_view::npos || colon == 0) {
      return false;
    }

    const std::string name = trim(trimmed.substr(0, colon));
    const std::string value = trim(trimmed.substr(colon + 1));
    if (name.empty() || value.empty()) {
      return false;
    }
    char* end = nullptr;
    const float scale = std::strtof(value.c_str(), &end);
    return end != value.c_str() && end != nullptr && *end == '\0' && scale >= 1.0f;
  }

  [[nodiscard]] std::size_t countValidOutputScaleEntries(std::string_view raw) {
    std::size_t count = 0;
    forEachOutputMappingEntry(raw, [&count](const std::string_view entry) {
      if (parseOutputScaleEntry(entry)) {
        ++count;
      } else {
        kLog.warn("ignoring invalid output scale entry '{}'", entry);
      }
    });
    return count;
  }

  [[nodiscard]] std::size_t countValidOutputTransformEntries(std::string_view raw) {
    std::size_t count = 0;
    forEachOutputMappingEntry(raw, [&count](const std::string_view entry) {
      if (parseOutputTransformEntry(entry)) {
        ++count;
      } else {
        kLog.warn("ignoring invalid output transform entry '{}'", entry);
      }
    });

    return count;
  }

  [[nodiscard]] bool setPathMode(const std::filesystem::path& path, const mode_t mode, std::string& errorOut) {
    return greeter::privileged_state::setMode(path, mode, errorOut);
  }

  [[nodiscard]] bool
  setPathOwner(const std::filesystem::path& path, const std::string& account, std::string& errorOut) {
    struct passwd* pw = ::getpwnam(account.c_str());
    if (pw == nullptr) {
      errorOut = "account '" + account + "' does not exist";
      return false;
    }
    return greeter::privileged_state::setOwnership(path, pw->pw_uid, pw->pw_gid, errorOut);
  }

  constexpr const char* kLegacyStateTomlFileName = "state.toml";

  void migrateLegacyRuntimeKeysToSync() {
    const auto confPath = greeter::greeterConfPath();
    const auto syncPath = greeter::greeterSyncPath();
    std::error_code ec;
    if (std::filesystem::is_regular_file(syncPath, ec) && !ec) {
      return;
    }

    // Prior releases named this file state.toml; adopt it once under the new name.
    const auto legacyStatePath = syncPath.parent_path() / kLegacyStateTomlFileName;
    const bool hasLegacyState = std::filesystem::is_regular_file(legacyStatePath, ec) && !ec;
    greeter::config::GreeterSyncFile sync =
        hasLegacyState ? greeter::config::loadSync(legacyStatePath) : greeter::config::GreeterSyncFile{};

    const greeter::config::GreeterConfigFile conf = greeter::config::loadConfig(confPath);
    const bool migrateOutputs = conf.outputUseSyncedSettings.value_or(true);
    const bool hasRuntime = (conf.sessionLast.has_value() && !conf.sessionLast->empty())
        || (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty())
        || (migrateOutputs
            && ((conf.outputLayout.has_value() && !conf.outputLayout->empty())
                || (conf.outputTransforms.has_value() && !conf.outputTransforms->empty())
                || (conf.outputScales.has_value() && !conf.outputScales->empty())));
    if (!hasLegacyState && !hasRuntime) {
      return;
    }

    if (conf.sessionLast.has_value() && !conf.sessionLast->empty()) {
      sync.sessionLast = conf.sessionLast;
    }
    if (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty()) {
      sync.appearanceScheme = conf.appearanceScheme;
    }
    if (migrateOutputs && conf.outputLayout.has_value() && !conf.outputLayout->empty()) {
      sync.outputLayout = conf.outputLayout;
    }
    if (migrateOutputs && conf.outputTransforms.has_value() && !conf.outputTransforms->empty()) {
      sync.outputTransforms = conf.outputTransforms;
    }
    if (migrateOutputs && conf.outputScales.has_value() && !conf.outputScales->empty()) {
      sync.outputScales = conf.outputScales;
    }
    if (!greeter::config::writeSync(syncPath, sync)) {
      kLog.warn("failed to migrate runtime keys to {}", syncPath.string());
      return;
    }

    if (hasRuntime) {
      // Keep declarative settings intact so they still override later Sync/UI changes.
      kLog.info("copied legacy runtime keys from {} into {}", confPath.string(), syncPath.string());
    } else {
      kLog.info("migrated {} to {}", legacyStatePath.string(), syncPath.string());
    }
  }

} // namespace

namespace greeter {

  namespace {

    std::optional<std::string> g_cliDefaultSession;
    std::optional<std::string> g_cliDefaultUser;

  } // namespace

  std::filesystem::path greeterConfPath() { return appearance::packageConfPath(); }

  std::filesystem::path greeterSyncPath() { return appearance::syncConfPath(); }

  void setCliDefaultSession(std::optional<std::string> session) { g_cliDefaultSession = std::move(session); }

  void setCliDefaultUser(std::optional<std::string> user) { g_cliDefaultUser = std::move(user); }

  std::optional<std::string> resolveInitialSessionName(const GreeterPreferences& prefs) {
    if (g_cliDefaultSession.has_value() && !g_cliDefaultSession->empty()) {
      return g_cliDefaultSession;
    }
    if (prefs.defaultSession.has_value() && !prefs.defaultSession->empty()) {
      return prefs.defaultSession;
    }
    if (prefs.session.has_value() && !prefs.session->empty()) {
      return prefs.session;
    }
    return std::nullopt;
  }

  std::optional<std::string> resolveInitialUserName(const GreeterPreferences& prefs) {
    if (g_cliDefaultUser.has_value() && !g_cliDefaultUser->empty()) {
      return g_cliDefaultUser;
    }
    if (prefs.defaultUser.has_value() && !prefs.defaultUser->empty()) {
      return prefs.defaultUser;
    }
    return std::nullopt;
  }

  std::vector<GreeterOutputPlacement> loadGreeterOutputLayout() {
    migrateLegacyRuntimeKeysToSync();
    const config::GreeterConfigFile conf = config::loadConfig(greeterConfPath());
    if (conf.outputLayout.has_value() && !conf.outputLayout->empty()) {
      return parseOutputLayoutValue(*conf.outputLayout);
    }
    if (!conf.outputUseSyncedSettings.value_or(true)) {
      return {};
    }
    const config::GreeterSyncFile sync = config::loadSync(greeterSyncPath());
    if (!sync.outputLayout.has_value() || sync.outputLayout->empty()) {
      return {};
    }
    return parseOutputLayoutValue(*sync.outputLayout);
  }

  bool applyAppearanceSyncGreeterConf(
      const std::optional<std::string>& stagedOutputLayout, const std::optional<std::string>& stagedOutputTransforms,
      const std::optional<std::string>& stagedOutputScales,
      const std::optional<GreeterSyncAppearanceUpdate>& appearanceUpdate
  ) {
    if (::geteuid() == 0) {
      std::string error;
      if (!greeter::privileged_state::removeSymlinkIfPresent(greeterSyncPath(), error)) {
        kLog.error("{}", error);
        return false;
      }
    }

    migrateLegacyRuntimeKeysToSync();
    const auto syncPath = greeterSyncPath();
    config::clearConfigDiagnostics();
    config::GreeterSyncFile sync = config::loadSync(syncPath);
    if (!config::configDiagnostics().empty()) {
      kLog.warn("refusing to overwrite invalid sync.toml");
      return false;
    }
    sync.appearanceScheme = appearance::kSyncedSchemeDisplayName;
    if (stagedOutputLayout.has_value()) {
      if (stagedOutputLayout->empty() || parseOutputLayoutValue(*stagedOutputLayout).empty()) {
        kLog.warn("refusing to apply invalid staged output layout");
        return false;
      }
      sync.outputLayout = *stagedOutputLayout;
    }
    if (stagedOutputTransforms.has_value()) {
      if (stagedOutputTransforms->empty() || countValidOutputTransformEntries(*stagedOutputTransforms) == 0) {
        kLog.warn("refusing to apply invalid staged output transforms");
        return false;
      }
      sync.outputTransforms = *stagedOutputTransforms;
    }
    if (stagedOutputScales.has_value()) {
      if (stagedOutputScales->empty() || countValidOutputScaleEntries(*stagedOutputScales) == 0) {
        kLog.warn("refusing to apply invalid staged output scales");
        return false;
      }
      sync.outputScales = *stagedOutputScales;
    }
    if (appearanceUpdate.has_value()) {
      sync.appearance = appearanceUpdate->appearance;
      if (appearanceUpdate->layout)
        sync.layout = appearanceUpdate->layout->empty() ? std::nullopt : appearanceUpdate->layout;
      if (appearanceUpdate->widgets)
        sync.widgets = appearanceUpdate->widgets->instances ? appearanceUpdate->widgets : std::nullopt;
      if (appearanceUpdate->replaceSession) {
        sync.sessionPowerSuspend = appearanceUpdate->sessionPowerSuspend;
        sync.sessionPowerReboot = appearanceUpdate->sessionPowerReboot;
        sync.sessionPowerShutdown = appearanceUpdate->sessionPowerShutdown;
        sync.sessionActions = appearanceUpdate->sessionActions;
      }
    }
    return config::writeSync(syncPath, sync);
  }

  GreeterLayoutConfig
  effectiveGreeterLayout(const GreeterPreferences& prefs, std::string_view connector, std::string_view identifier) {
    auto layout = prefs.layout;
    const auto local = prefs.layout.outputs.find(std::string(identifier));
    const auto named = prefs.layout.outputs.find(std::string(connector));
    if (local != prefs.layout.outputs.end())
      layout = local->second;
    if (named != prefs.layout.outputs.end())
      layout = named->second;
    layout.outputs.clear();
    auto& clock = layout.components[1];
    if (!clock.visible)
      clock.visible = prefs.clockEnabled;
    if (!clock.anchor && prefs.clockPosition != "center-left")
      clock.anchor = prefs.clockPosition;
    auto& brand = layout.components[2];
    if (!brand.visible)
      brand.visible = !prefs.hideLogo;
    mapLegacyCorner(layout.components[3], prefs.powerButtonsPosition);
    mapLegacyCorner(layout.components[4], prefs.schemeSelectorPosition);
    return layout;
  }

  GreeterPreferences loadGreeterPreferences() {
    migrateLegacyRuntimeKeysToSync();
    const config::GreeterConfigFile file = config::loadConfig(greeterConfPath());
    const config::GreeterSyncFile sync = config::loadSync(greeterSyncPath());

    GreeterPreferences prefs;
    prefs.layout = resolveStoredLayout(file, sync);
    prefs.administratorWidgets = file.widgets.has_value() || !file.widgetsValid;
    if (!prefs.administratorWidgets) {
      DefaultClockOverrides overrides;
      if (file.layout)
        overrides.placement = file.layout->components[1];
      if (!overrides.placement.visible)
        overrides.placement.visible = file.clockEnabled;
      if (!overrides.placement.anchor && file.clockPosition && validLegacyClockPosition(*file.clockPosition))
        overrides.placement.anchor = file.clockPosition;
      overrides.timeFormat = file.clockTimeFormat;
      overrides.dateFormat = file.clockDateFormat;
      overrides.timeFontSize = file.clockTimeFontSize;
      overrides.dateFontSize = file.clockDateFontSize;
      prefs.defaultClockOverrides = overrides;
    }
    if (file.widgetsValid)
      prefs.widgets = file.widgets.has_value() ? file.widgets : sync.widgets;
    prefs.defaultSession = file.sessionDefault;
    prefs.defaultUser = file.userDefault;
    prefs.session = sync.sessionLast;
    // greeter.toml is declarative and wins; sync.toml only carries the last UI pick.
    prefs.scheme = file.appearanceScheme.has_value() ? file.appearanceScheme : sync.appearanceScheme;
    prefs.output = file.outputName;
    prefs.scale = file.outputScale;
    if (file.appearancePasswordStyle.has_value()) {
      if (*file.appearancePasswordStyle == "random") {
        prefs.passwordMaskStyle = PasswordMaskStyle::RandomIcons;
      } else if (*file.appearancePasswordStyle != "default") {
        kLog.warn("invalid appearance.password_style '{}' (using filled circles)", *file.appearancePasswordStyle);
      }
    }
    if (file.appearanceHideLogo.has_value()) {
      prefs.hideLogo = *file.appearanceHideLogo;
    }
    prefs.powerButtonsPosition = file.appearancePowerButtonsPosition;
    prefs.schemeSelectorPosition = file.appearanceSchemeSelectorPosition;
    if (file.clockEnabled.has_value()) {
      prefs.clockEnabled = *file.clockEnabled;
    }
    if (file.clockPosition.has_value()) {
      if (validLegacyClockPosition(*file.clockPosition)) {
        prefs.clockPosition = *file.clockPosition;
      } else {
        kLog.warn("invalid clock.position '{}' (using center-left)", *file.clockPosition);
      }
    }
    if (file.clockTimeFormat.has_value()) {
      prefs.clockTimeFormat = *file.clockTimeFormat;
    }
    if (file.clockDateFormat.has_value()) {
      prefs.clockDateFormat = *file.clockDateFormat;
    }
    if (file.clockTimeFontSize.has_value()) {
      prefs.clockTimeFontSize = *file.clockTimeFontSize;
    }
    if (file.clockDateFontSize.has_value()) {
      prefs.clockDateFontSize = *file.clockDateFontSize;
    }
    if (file.authAllowEmptyPassword.has_value()) {
      prefs.allowEmptyPassword = *file.authAllowEmptyPassword;
    }
    if (file.authRequestTimeoutSec.has_value()) {
      prefs.authRequestTimeoutSec = *file.authRequestTimeoutSec;
    }
    return prefs;
  }

  GreeterWidgetProfile resolveGreeterWidgetProfile(
      const GreeterPreferences& prefs, std::string_view connector, std::string_view identifier
  ) {
    const bool builtin = !prefs.widgets || !prefs.widgets->instances;
    GreeterWidgetProfile profile;
    profile.placeDefaultClocks = builtin;
    const auto makeClock = [](std::string_view id, bool date) {
      WidgetInstance widget;
      widget.id = id;
      widget.type = "clock";
      widget.placement = WidgetAnchorPlacement{WidgetAnchor::CenterLeft, date ? 5.0 : 0.0, date ? 50.0 : -15.0};
      widget.settings = {
          {"format", std::string(date ? "%A, %x" : "{:%H:%M}")},
          {"font_size", date ? 19.0 : 112.0},
          {"font_weight", std::int64_t{date ? 500 : 300}},
          {"tight_text", true},
          {"center_text", false},
          {"background", false},
          {"shadow", date}
      };
      return widget;
    };
    if (builtin)
      profile.instances = {makeClock(kDefaultTimeWidget, false), makeClock(kDefaultDateWidget, true)};
    else
      profile.instances = *prefs.widgets->instances;
    for (auto& widget : profile.instances) {
      if (!widget.output || (*widget.output != connector && *widget.output != identifier))
        continue;
      for (const auto id : {kDefaultTimeWidget, kDefaultDateWidget}) {
        const auto prefix = std::string(id) + "-";
        if (widget.id.starts_with(prefix)
            && widget.id.size() == prefix.size() + 32
            && std::ranges::all_of(std::string_view(widget.id).substr(prefix.size()), [](char c) {
                 return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               })) {
          widget.id = id;
        }
      }
    }
    if (!prefs.defaultClockOverrides || prefs.administratorWidgets)
      return profile;
    const auto& overrides = *prefs.defaultClockOverrides;
    const auto& placement = overrides.placement;
    if (placement.anchor
        || placement.x
        || placement.y
        || placement.width
        || placement.output
        || placement.scale
        || placement.rotation
        || placement.flipX
        || placement.flipY)
      profile.placeDefaultClocks = true;
    for (const bool date : {false, true}) {
      const auto id = date ? kDefaultDateWidget : kDefaultTimeWidget;
      auto it = std::ranges::find(profile.instances, id, &WidgetInstance::id);
      if (it == profile.instances.end() && placement.visible == true) {
        profile.instances.push_back(makeClock(id, date));
        it = std::prev(profile.instances.end());
        profile.placeDefaultClocks = true;
      }
      if (it == profile.instances.end() || it->type != "clock")
        continue;
      if (placement.visible)
        it->enabled = *placement.visible;
      if (placement.output)
        it->output = placement.output;
      // The legacy group supplies explicitly owned transforms once.
      if (placement.rotation)
        it->transform.rotation = 0;
      if (placement.flipX)
        it->transform.flipX = false;
      if (placement.flipY)
        it->transform.flipY = false;
      const auto& format = date ? overrides.dateFormat : overrides.timeFormat;
      if (format) {
        it->settings["format"] = *format;
        if (format->empty())
          it->enabled = false;
      }
      const auto& fontSize = date ? overrides.dateFontSize : overrides.timeFontSize;
      if (fontSize)
        it->settings["font_size"] = static_cast<double>(std::min(*fontSize, 4096.0F));
    }
    return profile;
  }

  bool installGreeterSystemLayout(const std::string_view greeterUser, std::string& errorOut) {
    if (::geteuid() != 0) {
      errorOut = "installGreeterSystemLayout requires root";
      return false;
    }

    if (greeterUser.empty()) {
      errorOut = "greeter account name is empty";
      return false;
    }

    const auto dataDir = appearance::syncedDataDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    if (ec) {
      errorOut = std::string("failed to create '") + dataDir.string() + "': " + ec.message();
      return false;
    }
    if (!setPathMode(dataDir, kSyncedDirMode, errorOut)) {
      return false;
    }

    const std::string greeterAccount(greeterUser);
    if (!setPathOwner(dataDir, greeterAccount, errorOut)) {
      return false;
    }

    const auto confPath = greeterConfPath();
    const bool confExisted = std::filesystem::exists(confPath, ec) && !ec;
    // Load before unlinking a symlinked config so its content carries over into the real file.
    config::clearConfigDiagnostics();
    const config::GreeterConfigFile file = config::loadConfig(confPath);
    if (!config::configDiagnostics().empty()) {
      errorOut = "refusing to overwrite invalid greeter.toml: " + config::configDiagnostics().front().message;
      return false;
    }
    if (!greeter::privileged_state::removeSymlinkIfPresent(confPath, errorOut)) {
      return false;
    }
    if (!config::writeConfig(confPath, file)) {
      errorOut = "failed to write greeter.toml";
      return false;
    }
    if (!setPathMode(confPath, kGreeterConfMode, errorOut)) {
      return false;
    }
    if (!setPathOwner(confPath, greeterAccount, errorOut)) {
      return false;
    }

    migrateLegacyRuntimeKeysToSync();
    const auto syncPath = greeterSyncPath();
    if (!greeter::privileged_state::removeSymlinkIfPresent(syncPath, errorOut)) {
      return false;
    }
    if (!std::filesystem::is_regular_file(syncPath, ec) || ec) {
      if (!config::writeSync(syncPath, {})) {
        errorOut = "failed to write sync.toml";
        return false;
      }
    }
    if (!setPathMode(syncPath, kGreeterConfMode, errorOut)) {
      return false;
    }
    if (!setPathOwner(syncPath, greeterAccount, errorOut)) {
      return false;
    }

    kLog.info(
        "{} greeter.toml at '{}' for user '{}'", confExisted ? "updated" : "created", confPath.string(), greeterAccount
    );
    return true;
  }

  bool saveGreeterPreferences(const GreeterPreferences& prefs) {
    migrateLegacyRuntimeKeysToSync();
    const auto path = greeterSyncPath();
    config::GreeterSyncFile sync = config::loadSync(path);
    if (std::ranges::any_of(config::configDiagnostics(), [&](const auto& d) { return d.path == path; })) {
      kLog.warn("refusing to overwrite invalid {}", path.string());
      return false;
    }

    if (prefs.session.has_value() && !prefs.session->empty()) {
      sync.sessionLast = *prefs.session;
    } else {
      sync.sessionLast.reset();
    }

    if (prefs.scheme.has_value() && !prefs.scheme->empty()) {
      sync.appearanceScheme = *prefs.scheme;
    } else {
      sync.appearanceScheme.reset();
    }

    if (!config::writeSync(path, sync)) {
      std::error_code ec;
      if (!std::filesystem::exists(path, ec)) {
        kLog.warn(
            "cannot create {}; greetd user needs write access to {} (run: "
            "noctalia-greeter-apply-appearance --setup-system)",
            path.string(), path.parent_path().string()
        );
      }
      return false;
    }

    kLog.debug("saved greeter preferences to {}", path.string());
    return true;
  }

} // namespace greeter
