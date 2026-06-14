#pragma once

#include <functional>
#include <gio/gio.h>

// Subscribes to logind PrepareForSleep and invokes the callback on resume.
class LogindResumeMonitor {
public:
  using Callback = std::function<void()>;

  LogindResumeMonitor();
  ~LogindResumeMonitor();

  LogindResumeMonitor(const LogindResumeMonitor&) = delete;
  LogindResumeMonitor& operator=(const LogindResumeMonitor&) = delete;

  bool start(Callback onResume);
  void stop();

  [[nodiscard]] bool active() const noexcept;
  void prepareDispatch(int& maxPriority, GPollFD& pollFd, int& timeoutMs);
  void checkDispatch(int maxPriority, GPollFD& pollFd);

private:
  static void onPrepareForSleep(
      GDBusConnection* connection, const char* sender, const char* objectPath, const char* interfaceName,
      const char* signalName, GVariant* parameters, void* userData
  );

  void handleResume();

  Callback m_onResume;
  void* m_connection = nullptr;
  void* m_context = nullptr;
  unsigned int m_subscription = 0;
};
