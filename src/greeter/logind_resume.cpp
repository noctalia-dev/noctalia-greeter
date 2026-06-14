#include "greeter/logind_resume.h"

#include "core/log.h"

namespace {
  constexpr Logger kLog("logind-resume");

  constexpr char kLoginBusName[] = "org.freedesktop.login1";
  constexpr char kLoginObjectPath[] = "/org/freedesktop/login1";
  constexpr char kLoginInterface[] = "org.freedesktop.login1.Manager";
  constexpr char kPrepareForSleepSignal[] = "PrepareForSleep";
} // namespace

void LogindResumeMonitor::onPrepareForSleep(
    GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*objectPath*/, const char* /*interfaceName*/,
    const char* /*signalName*/, GVariant* parameters, void* userData
) {
  auto* monitor = static_cast<LogindResumeMonitor*>(userData);
  gboolean start = FALSE;
  g_variant_get(parameters, "(b)", &start);
  if (!start) {
    monitor->handleResume();
  }
}

LogindResumeMonitor::LogindResumeMonitor() = default;

LogindResumeMonitor::~LogindResumeMonitor() { stop(); }

bool LogindResumeMonitor::start(Callback onResume) {
  stop();
  if (!onResume) {
    return false;
  }

  GError* error = nullptr;
  auto* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (connection == nullptr) {
    if (error != nullptr) {
      kLog.warn("failed to connect to system D-Bus: {}", error->message);
      g_error_free(error);
    } else {
      kLog.warn("failed to connect to system D-Bus");
    }
    return false;
  }

  m_context = g_main_context_ref(g_main_context_default());
  m_onResume = std::move(onResume);
  m_connection = connection;
  m_subscription = g_dbus_connection_signal_subscribe(
      connection, kLoginBusName, kLoginInterface, kPrepareForSleepSignal, kLoginObjectPath, nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE, LogindResumeMonitor::onPrepareForSleep, this, nullptr
  );
  if (m_subscription == 0) {
    kLog.warn("failed to subscribe to logind PrepareForSleep");
    stop();
    return false;
  }

  kLog.info("watching logind PrepareForSleep to restart greeter on resume");
  return true;
}

void LogindResumeMonitor::stop() {
  if (m_subscription != 0 && m_connection != nullptr) {
    g_dbus_connection_signal_unsubscribe(static_cast<GDBusConnection*>(m_connection), m_subscription);
    m_subscription = 0;
  }
  if (m_connection != nullptr) {
    g_object_unref(static_cast<GDBusConnection*>(m_connection));
    m_connection = nullptr;
  }
  if (m_context != nullptr) {
    g_main_context_unref(static_cast<GMainContext*>(m_context));
    m_context = nullptr;
  }
  m_onResume = nullptr;
}

bool LogindResumeMonitor::active() const noexcept { return m_connection != nullptr; }

void LogindResumeMonitor::prepareDispatch(int& maxPriority, GPollFD& pollFd, int& timeoutMs) {
  pollFd.fd = -1;
  if (m_context == nullptr) {
    return;
  }

  auto* context = static_cast<GMainContext*>(m_context);
  if (!g_main_context_prepare(context, &maxPriority)) {
    if (g_main_context_pending(context)) {
      g_main_context_dispatch(context);
    }
    return;
  }

  gint glibTimeout = timeoutMs;
  if (!g_main_context_query(context, maxPriority, &glibTimeout, &pollFd, 1)) {
    return;
  }

  if (glibTimeout >= 0 && (timeoutMs < 0 || glibTimeout < timeoutMs)) {
    timeoutMs = glibTimeout;
  }
}

void LogindResumeMonitor::checkDispatch(int maxPriority, GPollFD& pollFd) {
  if (m_context == nullptr) {
    return;
  }

  auto* context = static_cast<GMainContext*>(m_context);
  if (g_main_context_check(context, maxPriority, &pollFd, 1)) {
    g_main_context_dispatch(context);
  }
}

void LogindResumeMonitor::handleResume() {
  kLog.info("system resumed from sleep; restarting greeter session");
  if (m_onResume) {
    m_onResume();
  }
}
