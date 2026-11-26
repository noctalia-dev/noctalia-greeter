#include "time/time_locale.h"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace {

  std::string_view trim(std::string_view text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
      return {};
    }
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
  }

  std::optional<std::string> readTimeLocale(const char* path) {
    std::ifstream file(path);
    std::optional<std::string> language;
    std::optional<std::string> timeLocale;
    std::string line;
    while (std::getline(file, line)) {
      std::string_view assignment = trim(line);
      if (assignment.starts_with("export ") || assignment.starts_with("export\t")) {
        assignment = trim(assignment.substr(6));
      }
      if (assignment.empty() || assignment.front() == '#') {
        continue;
      }
      const auto equals = assignment.find('=');
      if (equals == std::string_view::npos) {
        continue;
      }
      const auto key = trim(assignment.substr(0, equals));
      if (key != "LANG" && key != "LC_TIME") {
        continue;
      }

      auto value = trim(assignment.substr(equals + 1));
      if (!value.empty() && (value.front() == '\'' || value.front() == '"')) {
        const auto end = value.find(value.front(), 1);
        if (end == std::string_view::npos) {
          continue;
        }
        const auto remainder = trim(value.substr(end + 1));
        if (!remainder.empty() && remainder.front() != '#') {
          continue;
        }
        value = value.substr(1, end - 1);
      } else {
        value = trim(value.substr(0, value.find('#')));
      }

      // Locale identifiers are plain values. Never expand shell expressions.
      if (value.empty() || !std::ranges::all_of(value, [](const char c) {
            return (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || std::string_view("_.@+-").find(c) != std::string_view::npos;
          })) {
        continue;
      }
      (key == "LC_TIME" ? timeLocale : language) = std::string(value);
    }
    return timeLocale.has_value() ? timeLocale : language;
  }

  bool hasEnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
  }

} // namespace

bool initializeTimeLocale(const char* localeConfigPath, const char* fallbackConfigPath) {
  if (!hasEnvironmentValue("LC_ALL") && !hasEnvironmentValue("LC_TIME") && !hasEnvironmentValue("LANG")) {
    auto locale = readTimeLocale(localeConfigPath);
    if (!locale.has_value()) {
      locale = readTimeLocale(fallbackConfigPath);
    }
    if (!locale.has_value()) {
      for (const char* fallback : {"en_US.UTF-8", "C.UTF-8", "C"}) {
        if (std::setlocale(LC_TIME, fallback) != nullptr) {
          return ::setenv("LC_TIME", fallback, 1) == 0;
        }
      }
      return false;
    }
    if (::setenv("LC_TIME", locale->c_str(), 1) != 0) {
      return false;
    }
  }
  return std::setlocale(LC_TIME, "") != nullptr;
}
