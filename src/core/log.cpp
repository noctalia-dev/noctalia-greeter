#include "core/log.h"

#include <cctype>
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
  bool g_fileLogging = false;
  bool g_loggingInitialized = false;

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

  // File logging is opt-in: NOCTALIA_GREETER_LOG must be a real path.
  // "-", "none", "stderr", "stdout" (and unset) keep console logging
  // (info/debug → stdout, warn/error → stderr).
  [[nodiscard]] bool isStderrOnlyLogTarget(const char* value) {
    if (value == nullptr || value[0] == '\0') {
      return true;
    }
    if (std::strcmp(value, "-") == 0) {
      return true;
    }

    std::string lowered;
    lowered.reserve(std::strlen(value));
    for (const char* p = value; *p != '\0'; ++p) {
      lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return lowered == "none" || lowered == "stderr" || lowered == "stdout";
  }

  [[nodiscard]] const char* configuredLogFilePath() {
    const char* value = std::getenv("NOCTALIA_GREETER_LOG");
    if (isStderrOnlyLogTarget(value)) {
      return nullptr;
    }
    return value;
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

  void initLogFiles() {
    if (g_loggingInitialized) {
      return;
    }
    g_loggingInitialized = true;

    const char* explicitPath = configuredLogFilePath();
    if (explicitPath == nullptr) {
      g_fileLogging = false;
      return;
    }

    g_fileLogging = true;
    tryOpenLogFile(explicitPath);
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

  // Only touch files when file logging is explicitly configured.
  if (const char* path = configuredLogFilePath()) {
    emergencyAppend(path, text);
  }
}

void initLogging() {
  g_sessionMode = isSessionMode();
  initLogFiles();

  // Syslog duplicates journal when greetd already captures stderr; only use it
  // alongside an explicit log file.
  if (g_sessionMode && g_fileLogging) {
    openlog("noctalia-greeter", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    g_syslogOpen = true;
  }

  if (g_fileLogging) {
    if (!g_logPaths.empty()) {
      std::fprintf(stdout, "[info] [log] writing to: %s\n", g_logPaths.c_str());
      std::fflush(stdout);
    } else {
      std::fprintf(stderr, "[warn] [log] NOCTALIA_GREETER_LOG is set but no log file could be opened\n");
      std::fflush(stderr);
    }
  }
}

const char* loggingPaths() { return g_logPaths.c_str(); }

static void writeLogLine(std::string_view tag, std::string_view level, std::string_view message) {
  initLogFiles();

  std::ostringstream oss;
  if (g_fileLogging) {
    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
    localtime_r(&timeT, &tm);

    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count() << ' ';
  }

  oss << '[' << level << "] [" << tag << "] " << message << '\n';

  const std::string line = oss.str();
  FILE* stream = (level == "error" || level == "warn") ? stderr : stdout;
  std::fputs(line.c_str(), stream);
  std::fflush(stream);

  for (FILE* file : g_logFiles) {
    std::fputs(line.c_str(), file);
    std::fflush(file);
  }

  if (g_fileLogging) {
    writeSyslog(level, message);
  }
}

void installWlrLogHandler() {}

void Logger::log(std::string_view level, std::string_view message) const { writeLogLine(m_tag, level, message); }
