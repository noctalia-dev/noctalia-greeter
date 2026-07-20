#pragma once

#include <format>
#include <string_view>

// Early bootstrap line (stderr, syslog, and/or NOCTALIA_GREETER_LOG file).
void emergencyLogBootstrap(int argc, char* argv[]);

// Initialize logging. Under greetd: syslog by default. Optional file via
// NOCTALIA_GREETER_LOG=/path; console debug via NOCTALIA_GREETER_LOG=stderr.
void initLogging();

// Comma-separated list of file paths logging was opened to (empty if none).
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
