#include "greeter/greetd_user.h"

#include "core/log.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <pwd.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

  constexpr Logger kLog("greetd-user");

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

  [[nodiscard]] std::string stripComment(std::string_view line) {
    const std::size_t hash = line.find('#');
    if (hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    return trim(line);
  }

  [[nodiscard]] std::optional<std::string> unquoteTomlValue(std::string_view raw) {
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

  [[nodiscard]] bool containsNoctaliaGreeter(std::string_view command) {
    return command.find("noctalia-greeter") != std::string_view::npos;
  }

  struct GreetdSessionScan {
    std::optional<std::string> defaultSessionUser;
    std::optional<std::string> noctaliaSessionUser;
  };

  [[nodiscard]] std::optional<GreetdSessionScan> scanGreetdConfig(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
      return std::nullopt;
    }

    GreetdSessionScan result;
    std::string currentSection;
    bool sectionHasNoctaliaCommand = false;
    std::optional<std::string> sectionUser;

    const auto flushSection = [&]() {
      if (sectionHasNoctaliaCommand && sectionUser.has_value()) {
        result.noctaliaSessionUser = sectionUser;
      }
      if (currentSection == "default_session" && sectionUser.has_value()) {
        result.defaultSessionUser = sectionUser;
      }
      sectionHasNoctaliaCommand = false;
      sectionUser.reset();
    };

    std::string line;
    while (std::getline(in, line)) {
      line = stripComment(line);
      if (line.empty()) {
        continue;
      }

      if (line.front() == '[' && line.back() == ']') {
        flushSection();
        currentSection = trim(line.substr(1, line.size() - 2));
        continue;
      }

      const std::size_t eq = line.find('=');
      if (eq == std::string::npos) {
        continue;
      }

      const std::string key = trim(line.substr(0, eq));
      const auto value = unquoteTomlValue(line.substr(eq + 1));
      if (!value.has_value() || value->empty()) {
        continue;
      }

      if (key == "user") {
        sectionUser = value;
      } else if (key == "command" && containsNoctaliaGreeter(*value)) {
        sectionHasNoctaliaCommand = true;
      }
    }

    flushSection();
    return result;
  }

  [[nodiscard]] std::optional<std::string> readGreetdConfigGreeterUser(const std::filesystem::path& path) {
    const auto scan = scanGreetdConfig(path);
    if (!scan.has_value()) {
      return std::nullopt;
    }
    if (scan->noctaliaSessionUser.has_value()) {
      return scan->noctaliaSessionUser;
    }
    return scan->defaultSessionUser;
  }

  [[nodiscard]] std::vector<std::filesystem::path> greetdConfigPaths() {
    std::vector<std::filesystem::path> paths;
    if (const char* env = std::getenv("GREETD_CONFIG"); env != nullptr && env[0] != '\0') {
      paths.emplace_back(env);
    }
    paths.emplace_back("/etc/greetd/config.toml");
    return paths;
  }

  [[nodiscard]] bool accountExists(std::string_view name) { return ::getpwnam(std::string(name).c_str()) != nullptr; }

} // namespace

namespace greeter {

  std::optional<std::string> resolveGreeterAccountName() {
    if (const char* env = std::getenv(kGreeterUserEnv); env != nullptr && env[0] != '\0') {
      kLog.debug("greeter account from {}: {}", kGreeterUserEnv, env);
      return std::string(env);
    }

    for (const auto& path : greetdConfigPaths()) {
      if (const auto fromGreetd = readGreetdConfigGreeterUser(path)) {
        kLog.debug("greeter account from greetd config '{}': {}", path.string(), *fromGreetd);
        return fromGreetd;
      }
    }

    static constexpr const char* kFallbackUsers[] = {kDefaultGreeterUser, "greetd"};
    for (const char* candidate : kFallbackUsers) {
      if (accountExists(candidate)) {
        kLog.debug("greeter account from fallback: {}", candidate);
        return std::string(candidate);
      }
    }

    return std::nullopt;
  }

} // namespace greeter
