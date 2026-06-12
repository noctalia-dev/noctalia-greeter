#include "greeter/power_actions.h"

#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <poll.h>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace power {

  namespace {
    constexpr Logger kLog("power");
    constexpr std::chrono::milliseconds kCommandTimeout{5000};
    constexpr std::chrono::milliseconds kProcessPollInterval{100};

    bool commandExists(const char* name) {
      if (name == nullptr || name[0] == '\0') {
        return false;
      }
      if (std::strchr(name, '/') != nullptr) {
        return ::access(name, X_OK) == 0;
      }

      const char* pathEnv = std::getenv("PATH");
      if (pathEnv == nullptr || pathEnv[0] == '\0') {
        pathEnv = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
      }

      std::string_view path(pathEnv);
      std::size_t start = 0;
      while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string_view dir =
            end == std::string_view::npos ? path.substr(start) : path.substr(start, end - start);
        const std::filesystem::path candidate =
            dir.empty() ? std::filesystem::path(name) : (std::filesystem::path(dir) / name);
        if (::access(candidate.c_str(), X_OK) == 0) {
          return true;
        }
        if (end == std::string_view::npos) {
          break;
        }
        start = end + 1;
      }
      return false;
    }

    int exitCodeFromStatus(int status) {
      if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
      }
      return -1;
    }

    bool waitNoHang(pid_t pid, int& exitCode) {
      int status = 0;
      for (;;) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) {
          exitCode = exitCodeFromStatus(status);
          return true;
        }
        if (result == 0) {
          return false;
        }
        if (errno == EINTR) {
          continue;
        }
        exitCode = -1;
        return true;
      }
    }

    int waitBlocking(pid_t pid) {
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
          return -1;
        }
      }
      return exitCodeFromStatus(status);
    }

    void terminateAndWait(pid_t pid, int& exitCode) {
      ::kill(-pid, SIGTERM);
      ::kill(pid, SIGTERM);
      bool reaped = false;
      for (int i = 0; i < 10; ++i) {
        if (waitNoHang(pid, exitCode)) {
          reaped = true;
          break;
        }
        ::poll(nullptr, 0, 10);
      }
      ::kill(-pid, SIGKILL);
      ::kill(pid, SIGKILL);
      if (!reaped) {
        exitCode = waitBlocking(pid);
      }
    }

    int pollTimeoutMs(std::chrono::steady_clock::time_point deadline) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return 0;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      const auto bounded = std::clamp(remaining, std::chrono::milliseconds(1), kProcessPollInterval);
      return static_cast<int>(bounded.count());
    }

    bool runChecked(const std::vector<const char*>& args) {
      std::vector<const char*> argv(args);
      argv.push_back(nullptr);

      const pid_t pid = fork();
      if (pid < 0) {
        kLog.warn("failed to fork for {}: {}", args.front(), std::strerror(errno));
        return false;
      }
      if (pid == 0) {
        ::setpgid(0, 0);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
      }

      ::setpgid(pid, pid);

      const auto deadline = std::chrono::steady_clock::now() + kCommandTimeout;
      int exitCode = -1;
      for (;;) {
        if (waitNoHang(pid, exitCode)) {
          return exitCode == 0;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          terminateAndWait(pid, exitCode);
          kLog.warn("{} timed out", args.front());
          return false;
        }
        ::poll(nullptr, 0, std::min(10, pollTimeoutMs(deadline)));
      }
    }

    bool runFirstAvailable(const char* action, std::initializer_list<std::vector<const char*>> candidates) {
      bool attempted = false;
      for (const auto& argv : candidates) {
        if (argv.empty() || argv.front() == nullptr) {
          continue;
        }
        if (!commandExists(argv.front())) {
          kLog.debug("{}: {} not found", action, argv.front());
          continue;
        }
        attempted = true;
        if (runChecked(argv)) {
          kLog.info("{}: {} accepted", action, argv.front());
          return true;
        }
      }
      if (!attempted) {
        kLog.warn("{}: no supported command found", action);
      } else {
        kLog.warn("{}: all command methods failed", action);
      }
      return false;
    }
  } // namespace

  bool powerOff() {
    return runFirstAvailable(
        "shutdown",
        {
            {"systemctl", "poweroff"},
            {"loginctl", "poweroff"},
            {"poweroff"},
            {"/sbin/poweroff"},
            {"/usr/sbin/poweroff"},
        }
    );
  }

  bool reboot() {
    return runFirstAvailable(
        "reboot",
        {
            {"systemctl", "reboot"},
            {"loginctl", "reboot"},
            {"reboot"},
            {"/sbin/reboot"},
            {"/usr/sbin/reboot"},
        }
    );
  }

  bool rebootToFirmwareSetup() {
    return runFirstAvailable(
        "reboot-to-firmware",
        {
            {"systemctl", "reboot", "--firmware-setup"},
            {"loginctl", "reboot", "--firmware-setup"},
        }
    );
  }

  bool canRebootToFirmwareSetup() {
    std::error_code ec;
    return std::filesystem::exists("/sys/firmware/efi", ec)
        && (commandExists("systemctl") || commandExists("loginctl"));
  }

} // namespace power
