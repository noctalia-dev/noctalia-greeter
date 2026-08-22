#include "greeter/greeter_preferences.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/privileged_state_paths.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

  constexpr Logger kLog("greeter-prefs");

  constexpr mode_t kSyncedDirMode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
  constexpr mode_t kGreeterConfMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

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
    std::string normalized;
    normalized.reserve(raw.size());
    for (const char ch : raw) {
      normalized.push_back(ch == ';' ? ' ' : ch);
    }

    std::size_t begin = 0;
    while (begin < normalized.size()) {
      while (begin < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[begin])) != 0) {
        ++begin;
      }
      if (begin >= normalized.size()) {
        break;
      }

      std::size_t end = begin;
      while (end < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[end])) == 0) {
        ++end;
      }

      if (const auto placement = parseOutputLayoutEntry(normalized.substr(begin, end - begin))) {
        placements.push_back(*placement);
      } else {
        kLog.warn("ignoring invalid output layout entry '{}'", normalized.substr(begin, end - begin));
      }
      begin = end;
    }

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
    std::string normalized;
    normalized.reserve(raw.size());
    for (const char ch : raw) {
      normalized.push_back(ch == ';' ? ' ' : ch);
    }

    std::size_t begin = 0;
    while (begin < normalized.size()) {
      while (begin < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[begin])) != 0) {
        ++begin;
      }
      if (begin >= normalized.size()) {
        break;
      }

      std::size_t end = begin;
      while (end < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[end])) == 0) {
        ++end;
      }

      if (parseOutputScaleEntry(normalized.substr(begin, end - begin))) {
        ++count;
      } else {
        kLog.warn("ignoring invalid output scale entry '{}'", normalized.substr(begin, end - begin));
      }
      begin = end;
    }
    return count;
  }

  [[nodiscard]] std::size_t countValidOutputTransformEntries(std::string_view raw) {
    std::size_t count = 0;
    std::string normalized;
    normalized.reserve(raw.size());
    for (const char ch : raw) {
      normalized.push_back(ch == ';' ? ' ' : ch);
    }

    std::size_t begin = 0;
    while (begin < normalized.size()) {
      while (begin < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[begin])) != 0) {
        ++begin;
      }
      if (begin >= normalized.size()) {
        break;
      }

      std::size_t end = begin;
      while (end < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[end])) == 0) {
        ++end;
      }

      if (parseOutputTransformEntry(normalized.substr(begin, end - begin))) {
        ++count;
      } else {
        kLog.warn("ignoring invalid output transform entry '{}'", normalized.substr(begin, end - begin));
      }
      begin = end;
    }

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

    greeter::config::GreeterConfigFile conf = greeter::config::loadConfig(confPath);
    const bool hasRuntime = (conf.sessionLast.has_value() && !conf.sessionLast->empty())
        || (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty())
        || (conf.outputLayout.has_value() && !conf.outputLayout->empty())
        || (conf.outputTransforms.has_value() && !conf.outputTransforms->empty())
        || (conf.outputScales.has_value() && !conf.outputScales->empty());
    if (!hasLegacyState && !hasRuntime) {
      return;
    }

    if (conf.sessionLast.has_value() && !conf.sessionLast->empty()) {
      sync.sessionLast = conf.sessionLast;
    }
    if (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty()) {
      sync.appearanceScheme = conf.appearanceScheme;
    }
    if (conf.outputLayout.has_value() && !conf.outputLayout->empty()) {
      sync.outputLayout = conf.outputLayout;
    }
    if (conf.outputTransforms.has_value() && !conf.outputTransforms->empty()) {
      sync.outputTransforms = conf.outputTransforms;
    }
    if (conf.outputScales.has_value() && !conf.outputScales->empty()) {
      sync.outputScales = conf.outputScales;
    }
    if (!greeter::config::writeSync(syncPath, sync)) {
      kLog.warn("failed to migrate runtime keys to {}", syncPath.string());
      return;
    }

    if (hasRuntime) {
      conf.sessionLast.reset();
      conf.appearanceScheme.reset();
      conf.outputLayout.reset();
      conf.outputTransforms.reset();
      conf.outputScales.reset();
      if (!greeter::config::writeConfig(confPath, conf)) {
        kLog.warn("migrated sync.toml but failed to strip runtime keys from {}", confPath.string());
        return;
      }
      kLog.info("migrated runtime keys from greeter.toml into {}", syncPath.string());
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
    config::GreeterSyncFile sync = config::loadSync(greeterSyncPath());
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
      sync.sessionPowerSuspend = appearanceUpdate->sessionPowerSuspend;
      sync.sessionPowerReboot = appearanceUpdate->sessionPowerReboot;
      sync.sessionPowerShutdown = appearanceUpdate->sessionPowerShutdown;
      sync.sessionActions = appearanceUpdate->sessionActions;
    }
    return config::writeSync(greeterSyncPath(), sync);
  }

  GreeterPreferences loadGreeterPreferences() {
    migrateLegacyRuntimeKeysToSync();
    const config::GreeterConfigFile file = config::loadConfig(greeterConfPath());
    const config::GreeterSyncFile sync = config::loadSync(greeterSyncPath());

    GreeterPreferences prefs;
    prefs.defaultSession = file.sessionDefault;
    if (file.sessionShowSelector.has_value()) {
      prefs.showSessionSelector = *file.sessionShowSelector;
    }
    prefs.defaultUser = file.userDefault;
    if (file.uiShowSessionSelector.has_value()) {
      prefs.showSessionSelector = *file.uiShowSessionSelector;
    }
    if (file.uiShowThemeSelector.has_value()) {
      prefs.showThemeSelector = *file.uiShowThemeSelector;
    }
    if (file.uiShowShutdownButton.has_value()) {
      prefs.showShutdownButton = *file.uiShowShutdownButton;
    }
    if (file.uiShowRebootButton.has_value()) {
      prefs.showRebootButton = *file.uiShowRebootButton;
    }
    if (file.uiShowFirmwareButton.has_value()) {
      prefs.showFirmwareButton = *file.uiShowFirmwareButton;
    }
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
    if (file.authAllowEmptyPassword.has_value()) {
      prefs.allowEmptyPassword = *file.authAllowEmptyPassword;
    }
    return prefs;
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
    if (!greeter::privileged_state::removeSymlinkIfPresent(confPath, errorOut)) {
      return false;
    }
    const config::GreeterConfigFile file = config::loadConfig(confPath);
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
      if (!std::filesystem::exists(path)) {
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
