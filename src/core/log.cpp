#include "core/log.h"

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

extern "C" {}

namespace {
  std::vector<FILE*> g_logFiles;
  std::string g_logPaths;
  bool g_syslogOpen = false;
  bool g_sessionMode = false;

  void emergencyWrite(int fd, const void* data, std::size_t size) {
    [[maybe_unused]] const ssize_t written = ::write(fd, data, size);
  }

  bool isSessionMode() {
    if (std::getenv("GREETD_SOCK") != nullptr) {
      return true;
    }
    if (std::getenv("XDG_VTNR") != nullptr) {
      return true;
    }
    return false;
  }

  void emergencyAppend(const char* path, const char* text) {
    if (path == nullptr || path[0] == '\0') {
      return;
    }
    const int fd = ::open(path, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd < 0) {
      return;
    }
    emergencyWrite(fd, text, std::strlen(text));
    ::close(fd);
  }

  bool tryOpenLogFile(const char* path) {
    if (path == nullptr || path[0] == '\0') {
      return false;
    }

    if (g_logPaths.find(path) != std::string::npos) {
      return false;
    }

    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
      std::fprintf(stderr, "[warn] [log] cannot open '%s': %s\n", path, std::strerror(errno));
      std::fflush(stderr);
      return false;
    }

    g_logFiles.push_back(file);
    if (!g_logPaths.empty()) {
      g_logPaths += ", ";
    }
    g_logPaths += path;
    return true;
  }

  void tryRuntimeLog() {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir == nullptr || runtimeDir[0] == '\0') {
      return;
    }
    std::string path = std::string(runtimeDir) + "/noctalia-greeter.log";
    tryOpenLogFile(path.c_str());
  }

  void tryHomeStateLog() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      return;
    }
    std::string path = std::string(home) + "/.local/state/noctalia-greeter/greeter.log";
    tryOpenLogFile(path.c_str());
  }

  void tryHomeCacheLog() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      return;
    }
    std::string path = std::string(home) + "/.cache/noctalia-greeter.log";
    tryOpenLogFile(path.c_str());
  }

  void tryUidLog() {
    std::string path = "/tmp/noctalia-greeter-" + std::to_string(::getuid()) + ".log";
    tryOpenLogFile(path.c_str());
  }

  void initLogFiles() {
    if (!g_logFiles.empty()) {
      return;
    }

    const char* explicitPath = std::getenv("NOCTALIA_GREETER_LOG");
    if (explicitPath != nullptr && explicitPath[0] != '\0') {
      tryOpenLogFile(explicitPath);
      return;
    }

    if (!g_sessionMode) {
      return;
    }

    tryUidLog();
    tryHomeCacheLog();
    tryRuntimeLog();

    static constexpr const char* kCandidates[] = {
        "/var/log/noctalia-greeter.log",
        "/var/lib/noctalia-greeter/greeter.log",
        "/tmp/noctalia-greeter.log",
    };
    for (const char* path : kCandidates) {
      tryOpenLogFile(path);
    }
    tryHomeStateLog();
  }

  void writeSyslog(std::string_view level, std::string_view message) {
    if (!g_syslogOpen) {
      return;
    }
    int priority = LOG_INFO;
    if (level == "error") {
      priority = LOG_ERR;
    } else if (level == "warn") {
      priority = LOG_WARNING;
    }
    syslog(
        priority, "[%.*s] %.*s", static_cast<int>(level.size()), level.data(), static_cast<int>(message.size()),
        message.data()
    );
  }
} // namespace

void emergencyLogBootstrap(int argc, char* argv[]) {
  char exePath[512] = {};
  const ssize_t exeLen = ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);

  std::ostringstream msg;
  msg << "=== noctalia-greeter bootstrap pid=" << ::getpid() << " uid=" << ::getuid() << " euid=" << ::geteuid();
  if (exeLen > 0) {
    msg << " exe=" << exePath;
  }
  if (argc > 0 && argv[0] != nullptr) {
    msg << " argv0=" << argv[0];
  }
  msg << " GREETD_SOCK=" << (std::getenv("GREETD_SOCK") ? std::getenv("GREETD_SOCK") : "unset");
  msg << " XDG_VTNR=" << (std::getenv("XDG_VTNR") ? std::getenv("XDG_VTNR") : "unset");
  msg << " ===\n";

  const std::string line = msg.str();
  const char* text = line.c_str();

  emergencyWrite(STDERR_FILENO, text, line.size());

  std::string uidLog = "/tmp/noctalia-greeter-" + std::to_string(::getuid()) + ".log";
  emergencyAppend(uidLog.c_str(), text);

  const char* home = std::getenv("HOME");
  std::string homeLog;
  if (home != nullptr && home[0] != '\0') {
    homeLog = std::string(home) + "/.cache/noctalia-greeter.log";
    emergencyAppend(homeLog.c_str(), text);
  }

  static constexpr const char* kEmergencyPaths[] = {
      "/var/log/noctalia-greeter.log",
      "/var/lib/noctalia-greeter/greeter.log",
      "/tmp/noctalia-greeter.log",
  };
  for (const char* path : kEmergencyPaths) {
    emergencyAppend(path, text);
  }

  const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
  if (runtimeDir != nullptr && runtimeDir[0] != '\0') {
    std::string runtimePath = std::string(runtimeDir) + "/noctalia-greeter.log";
    emergencyAppend(runtimePath.c_str(), text);
  }
}

void initLogging() {
  g_sessionMode = isSessionMode();
  initLogFiles();

  if (g_sessionMode) {
    openlog("noctalia-greeter", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    g_syslogOpen = true;
  }

  if (!g_logPaths.empty()) {
    std::fprintf(stderr, "[info] [log] writing to: %s\n", g_logPaths.c_str());
    std::fflush(stderr);
  } else if (g_sessionMode) {
    std::fprintf(
        stderr,
        "[warn] [log] no log file opened; check permissions "
        "on /var/lib/noctalia-greeter "
        "and /var/log/noctalia-greeter.log\n"
    );
    std::fflush(stderr);
  }
}

const char* loggingPaths() { return g_logPaths.c_str(); }

static void writeLogLine(std::string_view tag, std::string_view level, std::string_view message) {
  initLogFiles();

  const auto now = std::chrono::system_clock::now();
  const auto timeT = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  std::tm tm{};
  localtime_r(&timeT, &tm);

  std::ostringstream oss;
  oss
      << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
      << '.'
      << std::setfill('0')
      << std::setw(3)
      << ms.count()
      << " ["
      << level
      << "] ["
      << tag
      << "] "
      << message
      << '\n';

  const std::string line = oss.str();
  std::fputs(line.c_str(), stderr);
  std::fflush(stderr);

  for (FILE* file : g_logFiles) {
    std::fputs(line.c_str(), file);
    std::fflush(file);
  }

  if (g_sessionMode) {
    writeSyslog(level, message);
  }
}

void installWlrLogHandler() {}

void Logger::log(std::string_view level, std::string_view message) const { writeLogLine(m_tag, level, message); }
