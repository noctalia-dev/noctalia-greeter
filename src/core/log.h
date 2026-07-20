#pragma once

#include <format>
#include <string_view>

// Raw write to stderr (and optional NOCTALIA_GREETER_LOG file) before anything else.
void emergencyLogBootstrap(int argc, char* argv[]);

// Initialize logging. Console by default; file when NOCTALIA_GREETER_LOG is a path.
// Under greetd, also mirrors to syslog for journald.
void initLogging();

// Comma-separated list of file paths logging was opened to (empty if stderr-only).
[[nodiscard]] const char* loggingPaths();

// Forward wlroots log lines into stderr and log files.
void installWlrLogHandler();

class Logger {
public:
  explicit constexpr Logger(std::string_view tag) : m_tag(tag) {}

  template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) const {
    log("info", std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) const {
    log("warn", std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) const {
    log("error", std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) const {
    log("debug", std::format(fmt, std::forward<Args>(args)...));
  }

private:
  void log(std::string_view level, std::string_view message) const;

  std::string_view m_tag;
};
