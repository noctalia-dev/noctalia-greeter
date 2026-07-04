#include "greeter/greeter_preferences.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"

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

  [[nodiscard]] bool setPathMode(const std::filesystem::path& path, const mode_t mode, std::string& errorOut) {
    if (::chmod(path.c_str(), mode) != 0) {
      errorOut = std::string("chmod failed for '") + path.string() + "': " + std::strerror(errno);
      return false;
    }
    return true;
  }

  [[nodiscard]] bool
  setPathOwner(const std::filesystem::path& path, const std::string& account, std::string& errorOut) {
    struct passwd* pw = ::getpwnam(account.c_str());
    if (pw == nullptr) {
      errorOut = "account '" + account + "' does not exist";
      return false;
    }
    if (::chown(path.c_str(), pw->pw_uid, pw->pw_gid) != 0) {
      errorOut = std::string("chown failed for '") + path.string() + "': " + std::strerror(errno);
      return false;
    }
    return true;
  }

} // namespace

namespace greeter {

  namespace {

    std::optional<std::string> g_cliDefaultSession;
    std::optional<std::string> g_cliDefaultUser;

  } // namespace

  std::filesystem::path greeterConfPath() { return appearance::packageConfPath(); }

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
    const config::GreeterConfigFile file = config::loadConfig(greeterConfPath());
    if (!file.outputLayout.has_value() || file.outputLayout->empty()) {
      return {};
    }
    return parseOutputLayoutValue(*file.outputLayout);
  }

  bool applyAppearanceSyncGreeterConf(const std::optional<std::string>& stagedOutputLayout) {
    config::GreeterConfigFile file = config::loadConfig(greeterConfPath());
    file.appearanceScheme = appearance::kSyncedSchemeDisplayName;
    if (stagedOutputLayout.has_value()) {
      if (stagedOutputLayout->empty() || parseOutputLayoutValue(*stagedOutputLayout).empty()) {
        kLog.warn("refusing to apply invalid staged output layout");
        return false;
      }
      file.outputLayout = *stagedOutputLayout;
    }
    return config::writeConfig(greeterConfPath(), file);
  }

  GreeterPreferences loadGreeterPreferences() {
    const config::GreeterConfigFile file = config::loadConfig(greeterConfPath());

    GreeterPreferences prefs;
    prefs.defaultSession = file.sessionDefault;
    prefs.defaultUser = file.userDefault;
    prefs.session = file.sessionLast;
    prefs.scheme = file.appearanceScheme;
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

    kLog.info(
        "{} greeter.toml at '{}' for user '{}'", confExisted ? "updated" : "created", confPath.string(), greeterAccount
    );
    return true;
  }

  bool saveGreeterPreferences(const GreeterPreferences& prefs) {
    const auto path = greeterConfPath();
    config::GreeterConfigFile file = config::loadConfig(path);

    if (prefs.session.has_value() && !prefs.session->empty()) {
      file.sessionLast = *prefs.session;
    } else {
      file.sessionLast.reset();
    }

    if (prefs.scheme.has_value() && !prefs.scheme->empty()) {
      file.appearanceScheme = *prefs.scheme;
    } else {
      file.appearanceScheme.reset();
    }

    if (!config::writeConfig(path, file)) {
      if (!std::filesystem::exists(path)) {
        kLog.warn(
            "cannot create {}; greetd user needs write access to {} (run: "
            "noctalia-greeter-apply-appearance --setup-system)",
            path.string(), path.parent_path().string()
        );
      }
      return false;
    }

    kLog.debug("saved greeter prefs to {}", path.string());
    return true;
  }

} // namespace greeter
