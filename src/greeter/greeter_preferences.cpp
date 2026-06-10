#include "greeter/greeter_preferences.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr Logger kLog("greeter-prefs");

constexpr mode_t kSyncedDirMode =
    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
constexpr mode_t kGreeterConfMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
constexpr const char *kLegacyGreeterConfPath = "/etc/noctalia-greeter.conf";

[[nodiscard]] std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] std::string stripComment(std::string_view line) {
  const std::size_t hash = line.find('#');
  if (hash != std::string_view::npos) {
    line = line.substr(0, hash);
  }
  return trim(line);
}

[[nodiscard]] std::optional<std::string> unquoteValue(std::string_view raw) {
  const std::string value = trim(raw);
  if (value.size() >= 2) {
    const char quote = value.front();
    if ((quote == '"' || quote == '\'') && value.back() == quote) {
      return value.substr(1, value.size() - 2);
    }
  }
  if (!value.empty()) {
    return value;
  }
  return std::nullopt;
}

[[nodiscard]] std::string formatConfValue(std::string_view value) {
  if (value.find_first_of(" \t#\"'") != std::string_view::npos) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char ch : value) {
      if (ch == '"' || ch == '\\') {
        out.push_back('\\');
      }
      out.push_back(ch);
    }
    out.push_back('"');
    return out;
  }
  return std::string(value);
}

using KeyValueMap = std::map<std::string, std::string>;

[[nodiscard]] KeyValueMap loadKeyValues(const std::filesystem::path &path) {
  KeyValueMap map;
  std::ifstream in(path);
  if (!in.is_open()) {
    return map;
  }

  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    const std::string stripped = stripComment(line);
    if (stripped.empty()) {
      continue;
    }
    const std::size_t eq = stripped.find('=');
    if (eq == std::string::npos) {
      kLog.warn("{}:{}: ignoring line without '=': '{}'", path.string(),
                lineNumber, stripped);
      continue;
    }
    const std::string key = trim(stripped.substr(0, eq));
    if (key.empty()) {
      kLog.warn("{}:{}: ignoring entry with empty key", path.string(),
                lineNumber);
      continue;
    }
    const auto value = unquoteValue(stripped.substr(eq + 1));
    if (!value.has_value()) {
      kLog.warn("{}:{}: ignoring key '{}' with empty value", path.string(),
                lineNumber, key);
      continue;
    }
    if (map.find(key) != map.end()) {
      kLog.warn("{}:{}: duplicate key '{}' overrides earlier value",
                path.string(), lineNumber, key);
    }
    map[key] = *value;
  }
  return map;
}

[[nodiscard]] bool writeKeyValues(const std::filesystem::path &path,
                                  const KeyValueMap &map) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    kLog.warn("failed to open '{}' for write", path.string());
    return false;
  }

  out << "# noctalia-greeter greeter.conf\n";
  out << "# default_session: admin default (Wayland session Name=)\n";
  out << "# session: last used (UI); scheme: color scheme name\n";
  out << "# output: Wayland connector; scale: UI scale; admin-only\n";

  static constexpr const char *kPreferredOrder[] = {
      "greeter_user", "default_session", "session",
      "scheme",       "output",          "scale"};
  for (const char *key : kPreferredOrder) {
    const auto it = map.find(key);
    if (it != map.end()) {
      out << key << '=' << formatConfValue(it->second) << '\n';
    }
  }

  for (const auto &[key, value] : map) {
    bool listed = false;
    for (const char *preferred : kPreferredOrder) {
      if (key == preferred) {
        listed = true;
        break;
      }
    }
    if (listed) {
      continue;
    }
    out << key << '=' << formatConfValue(value) << '\n';
  }

  return true;
}

[[nodiscard]] std::optional<std::string>
mapValue(const KeyValueMap &map, std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const auto it = map.find(key);
    if (it != map.end() && !it->second.empty()) {
      return it->second;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<float> parseScaleValue(std::string_view raw) {
  const std::string value = trim(raw);
  if (value.empty()) {
    return std::nullopt;
  }

  char *end = nullptr;
  errno = 0;
  const float scale = std::strtof(value.c_str(), &end);
  if (errno != 0 || end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return std::nullopt;
  }
  return scale;
}

[[nodiscard]] bool setPathMode(const std::filesystem::path &path,
                               const mode_t mode, std::string &errorOut) {
  if (::chmod(path.c_str(), mode) != 0) {
    errorOut = std::string("chmod failed for '") + path.string() +
               "': " + std::strerror(errno);
    return false;
  }
  return true;
}

[[nodiscard]] bool setPathOwner(const std::filesystem::path &path,
                                const std::string &account,
                                std::string &errorOut) {
  struct passwd *pw = ::getpwnam(account.c_str());
  if (pw == nullptr) {
    errorOut = "account '" + account + "' does not exist";
    return false;
  }
  if (::chown(path.c_str(), pw->pw_uid, pw->pw_gid) != 0) {
    errorOut = std::string("chown failed for '") + path.string() +
               "': " + std::strerror(errno);
    return false;
  }
  return true;
}

} // namespace

namespace greeter {

namespace {

std::optional<std::string> g_cliDefaultSession;

} // namespace

std::filesystem::path greeterConfPath() {
  return appearance::packageConfPath();
}

void setCliDefaultSession(std::optional<std::string> session) {
  g_cliDefaultSession = std::move(session);
}

std::optional<std::string>
resolveInitialSessionName(const GreeterPreferences &prefs) {
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

GreeterPreferences loadGreeterPreferences() {
  GreeterPreferences prefs;
  const auto path = greeterConfPath();
  const KeyValueMap map = loadKeyValues(path);

  static constexpr std::array<std::string_view, 6> kKnownKeys = {
      "greeter_user", "default_session", "session",
      "scheme",       "output",          "scale"};
  for (const auto &[key, value] : map) {
    if (std::find(kKnownKeys.begin(), kKnownKeys.end(),
                  std::string_view(key)) == kKnownKeys.end()) {
      kLog.warn("{}: unrecognized key '{}' (ignored)", path.string(), key);
    }
  }

  prefs.defaultSession = mapValue(map, {"default_session"});
  prefs.session = mapValue(map, {"session"});
  prefs.scheme = mapValue(map, {"scheme"});
  prefs.output = mapValue(map, {"output"});
  if (const auto rawScale = mapValue(map, {"scale"})) {
    if (const auto scale = parseScaleValue(*rawScale)) {
      prefs.scale = *scale;
    } else {
      kLog.warn("{}: invalid scale '{}' (using auto scale)", path.string(),
                *rawScale);
    }
  }
  return prefs;
}

bool installGreeterSystemLayout(const std::string_view greeterUser,
                                std::string &errorOut) {
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
    errorOut = std::string("failed to create '") + dataDir.string() +
               "': " + ec.message();
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
  KeyValueMap map = loadKeyValues(confPath);
  if (map.find("greeter_user") == map.end()) {
    map["greeter_user"] = std::string(greeterUser);
  }
  if (!writeKeyValues(confPath, map)) {
    errorOut = "failed to write greeter.conf";
    return false;
  }
  if (!setPathMode(confPath, kGreeterConfMode, errorOut)) {
    return false;
  }
  if (!setPathOwner(confPath, greeterAccount, errorOut)) {
    return false;
  }

  std::filesystem::remove(kLegacyGreeterConfPath, ec);

  kLog.info("{} greeter.conf at '{}' for user '{}'",
            confExisted ? "updated" : "created", confPath.string(),
            greeterAccount);
  return true;
}

bool saveGreeterPreferences(const GreeterPreferences &prefs) {
  const auto path = greeterConfPath();
  KeyValueMap map = loadKeyValues(path);

  if (prefs.session.has_value() && !prefs.session->empty()) {
    map["session"] = *prefs.session;
  } else {
    map.erase("session");
  }

  if (prefs.scheme.has_value() && !prefs.scheme->empty()) {
    map["scheme"] = *prefs.scheme;
  } else {
    map.erase("scheme");
  }

  if (!writeKeyValues(path, map)) {
    if (!std::filesystem::exists(path)) {
      kLog.warn("cannot create {}; greetd user needs write access to {} (run: "
                "noctalia-greeter-apply-appearance --setup-system)",
                path.string(), path.parent_path().string());
    }
    return false;
  }

  kLog.debug("saved greeter prefs to {}", path.string());
  return true;
}

} // namespace greeter
