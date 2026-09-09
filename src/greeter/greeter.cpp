#include "greeter/greeter.h"

#include "core/log.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/logind_resume.h"
#include "render/render_context.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/input.h"
#include "wayland/wayland_client.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <wayland-client.h>

namespace {
  constexpr Logger kLog("greeter");

  void logWaylandDispatchError(wl_display* display, const char* stage) {
    const wl_interface* interface = nullptr;
    std::uint32_t code = 0;
    const std::uint32_t protocolError = wl_display_get_protocol_error(display, &interface, &code);
    if (protocolError != 0) {
      const char* interfaceName = interface != nullptr && interface->name != nullptr ? interface->name : "unknown";
      kLog.error(
          "Wayland {} failed: protocol error {} interface={} code={}", stage, protocolError, interfaceName, code
      );
      return;
    }
    const int displayError = wl_display_get_error(display);
    kLog.error("Wayland {} failed (display error={} errno={} '{}')", stage, displayError, errno, std::strerror(errno));
  }

  void applyConfiguredOutput(WaylandClient& client, const std::optional<std::string>& configured) {
    if (!configured.has_value() || configured->empty()) {
      client.forgetPreferredOutput();
      return;
    }

    client.setPreferredOutputName(configured);
    if (!client.hasReadyOutputs()) {
      return;
    }

    if (!client.hasResolvedPreferredOutput()) {
      kLog.warn("output '{}' is not connected; showing on all outputs", *configured);
      client.forgetPreferredOutput();
      return;
    }

    kLog.info("preferred output connector: {}", *configured);
  }
} // namespace

Greeter::Greeter() = default;

Greeter::~Greeter() = default;

bool Greeter::initialize(WaylandClient& client) {
  m_client = &client;
  m_initializing = true;

  greeter::config::clearConfigDiagnostics();

  const greeter::GreeterPreferences prefs = greeter::loadGreeterPreferences();

  m_greetdClient.setRequestTimeout(std::chrono::seconds(prefs.authRequestTimeoutSec));

  Input::setPasswordMaskStyle(
      prefs.passwordMaskStyle == greeter::PasswordMaskStyle::RandomIcons ? Input::PasswordMaskStyle::RandomIcons
                                                                         : Input::PasswordMaskStyle::CircleFilled
  );

  GlyphRegistry::initialize();

  m_glSharedContext.initialize(client.display());

  m_renderContext = std::make_unique<RenderContext>();
  m_renderContext->initialize(m_glSharedContext);
  if (const auto synced = loadGreeterSyncedAppearance(); synced.has_value() && !synced->fontFamily.empty()) {
    m_renderContext->setTextFontFamily(synced->fontFamily);
  }

  connectGreetd();

  client.setOutputsChangedCallback([this, configured = prefs.output]() {
    if (m_initializing) {
      m_pendingOutputSync = true;
      return;
    }
    applyConfiguredOutput(*m_client, configured);
    syncOutputWindows();
  });

  applyConfiguredOutput(client, prefs.output);

  for (int attempt = 0; attempt < 5; ++attempt) {
    if (client.flush() < 0) {
      break;
    }
    if (wl_display_roundtrip(client.display()) < 0) {
      logWaylandDispatchError(client.display(), "roundtrip before syncOutputWindows");
      break;
    }
    if (client.readyOutputsSorted().size() > 1 || attempt == 4) {
      break;
    }
  }

  syncOutputWindows();

  if (m_views.empty()) {
    kLog.warn("no outputs ready yet; waiting for output events");
  }

  setupInputCallbacks(client);

  m_sceneReady = true;
  for (auto& view : m_views) {
    view.window->setSceneReady(true);
  }

  m_initializing = false;
  if (m_pendingOutputSync) {
    m_pendingOutputSync = false;
    syncOutputWindows();
  }

  if (client.flush() < 0) {
    kLog.error("Wayland flush failed after greeter init");
    return false;
  }

  kLog.info("greeter initialized ({} view(s))", m_views.size());
  return true;
}

int Greeter::run(WaylandClient& client, const std::atomic<bool>& shutdownRequested) {
  wl_display* display = client.display();

  LogindResumeMonitor resumeMonitor;
  if (std::getenv("GREETD_SOCK") != nullptr) {
    (void)resumeMonitor.start([this]() { m_exitRequested = true; });
  }

  while (!m_exitRequested && !shutdownRequested.load(std::memory_order_relaxed)) {
    client.repeatTick();

    if (client.flush() < 0) {
      kLog.error("Wayland flush failed");
      return 1;
    }

    const int repeatMs = client.repeatPollTimeoutMs();
    const int requestMs = m_greetdClient.requestPollTimeoutMs();
    int timeoutMs = repeatMs < 0 ? requestMs : requestMs < 0 ? repeatMs : std::min(repeatMs, requestMs);

    // An animated wallpaper has no external frame clock (frame callbacks stall
    // when the scene is otherwise idle, and nested backends never tick), so pace
    // it from this loop: wake at roughly one frame interval while it plays.
    bool animatingBackground = false;
    for (auto& view : m_views) {
      if (view.surface == nullptr) {
        continue;
      }
      const int frameMs = view.surface->animationPollTimeoutMs();
      if (frameMs < 0) {
        continue;
      }
      animatingBackground = true;
      const int clamped = std::max(frameMs, 4); // floor to avoid a busy loop
      timeoutMs = timeoutMs < 0 ? clamped : std::min(timeoutMs, clamped);
    }

    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) < 0) {
        logWaylandDispatchError(display, "dispatch_pending");
        return 1;
      }
    }

    GPollFD glibPoll{};
    int glibPriority = 0;
    int pollTimeout = timeoutMs;
    if (resumeMonitor.active()) {
      resumeMonitor.prepareDispatch(glibPriority, glibPoll, pollTimeout);
    }

    pollfd pfds[3]{};
    pfds[0].fd = wl_display_get_fd(display);
    pfds[0].events = POLLIN;
    int pollCount = 1;

    int glibIndex = -1;
    if (glibPoll.fd >= 0) {
      glibIndex = pollCount;
      pfds[pollCount].fd = glibPoll.fd;
      pfds[pollCount].events = static_cast<short>(glibPoll.events);
      glibPoll.revents = 0;
      ++pollCount;
    }

    int greetdIndex = -1;
    const int greetdFd = m_greetdClient.fd();
    if (greetdFd >= 0) {
      greetdIndex = pollCount;
      pfds[pollCount].fd = greetdFd;
      pfds[pollCount].events = POLLIN;
      ++pollCount;
    }

    const int pollResult = poll(pfds, static_cast<nfds_t>(pollCount), pollTimeout);
    if (pollResult > 0) {
      if (glibIndex >= 0 && pfds[glibIndex].revents != 0) {
        glibPoll.revents = pfds[glibIndex].revents;
        resumeMonitor.checkDispatch(glibPriority, glibPoll);
      }
      if ((pfds[0].revents & POLLIN) != 0) {
        wl_display_read_events(display);
      } else {
        wl_display_cancel_read(display);
      }
      if (greetdIndex >= 0 && (pfds[greetdIndex].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        onGreetdReadable();
      }
    } else {
      wl_display_cancel_read(display);
    }

    // A readable reply at the deadline wins because it was drained above and
    // disarmed the watchdog before this check.
    if (const auto timeout = m_greetdClient.timedOutRequest()) {
      onGreetdTimeout(*timeout);
    }

    if (wl_display_dispatch_pending(display) < 0) {
      logWaylandDispatchError(display, "dispatch_pending");
      return 1;
    }

    // Drive the animated background from the main loop: advance on wall-clock
    // time and repaint only when a new frame was actually decoded, so it renders
    // at the clip's rate rather than the display's. Safe here (not inside
    // prepareFrame), so requestRedraw cannot recurse.
    if (animatingBackground) {
      for (auto& view : m_views) {
        if (view.surface != nullptr && view.surface->advanceAnimation() && view.window != nullptr) {
          view.window->requestRedraw();
        }
      }
    }
  }

  return 0;
}

void Greeter::syncOutputWindows() {
  if (m_client == nullptr) {
    return;
  }
  if (m_syncingOutputWindows) {
    m_pendingOutputSync = true;
    return;
  }
  m_syncingOutputWindows = true;
  m_pendingOutputSync = false;

  struct TargetOutput {
    wl_output* output = nullptr;
    std::string name;
  };
  std::vector<TargetOutput> targets;
  for (const WaylandOutputInfo* output : m_client->greeterTargetOutputs()) {
    if (output != nullptr) {
      targets.push_back(TargetOutput{.output = output->output, .name = output->name});
    }
  }
  kLog.info("syncOutputWindows: {} target output(s), {} view(s)", targets.size(), m_views.size());

  const auto isTargetOutput = [&targets](const wl_output* output) {
    return std::ranges::any_of(targets, [output](const TargetOutput& target) { return target.output == output; });
  };

  // Toplevels are assigned to a specific compositor output when they are
  // created. Remove the exact views whose output disappeared; moving a view to
  // another vector slot does not migrate that compositor-side assignment.
  for (auto it = m_views.begin(); it != m_views.end();) {
    if (isTargetOutput(it->output)) {
      ++it;
      continue;
    }
    if (m_authSurface == it->surface.get()) {
      markGreetdUnavailable("Login interrupted after the display configuration changed. Restart greetd.");
    }
    if (m_activeSurface == it->surface.get()) {
      m_activeSurface = nullptr;
    }
    it = m_views.erase(it);
  }

  bool createdView = false;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const auto existing = std::find_if(
        m_views.begin() + static_cast<std::ptrdiff_t>(index), m_views.end(),
        [target = targets[index].output](const View& view) { return view.output == target; }
    );
    if (existing != m_views.end()) {
      std::iter_swap(m_views.begin() + static_cast<std::ptrdiff_t>(index), existing);
      continue;
    }

    View view;
    view.surface = std::make_unique<GreeterSurface>();
    view.surface->setGreetdClient(&m_greetdClient);
    view.surface->setOnExitRequested([this]() { m_exitRequested = true; });
    view.surface->setOnStateChanged([this](GreeterSurface* source) { syncStateFrom(source); });
    view.surface->setOnAuthBeginRequested([this](GreeterSurface* source) { return claimAuthSurface(source); });
    view.surface->setOnAuthEnded([this](GreeterSurface* source) { releaseAuthSurface(source); });
    view.surface->setOnGreetdTransportError([this](const GreetdError& error) { onGreetdTransportError(error); });
    view.surface->initialize(m_renderContext.get());
    view.surface->setSharedAuthBlocked(m_authSurface != nullptr);
    if (m_greetdUnavailable) {
      view.surface->setGreetdUnavailable("Login service is unavailable. Restart greetd.");
    }

    view.window = std::make_unique<GreeterWindow>(*m_client, m_glSharedContext, *m_renderContext, *view.surface);
    view.surface->setWindow(view.window.get());

    if (!view.window->createSurface()) {
      kLog.error("failed to create greeter window");
      m_syncingOutputWindows = false;
      return;
    }

    view.window->bindOutput(targets[index].output);
    view.surface->setBoundOutputName(targets[index].name);
    view.output = targets[index].output;
    kLog.info("greeter view for output '{}'", targets[index].name.empty() ? "?" : targets[index].name.c_str());

    if (!m_views.empty()) {
      view.surface->mirrorStateFrom(*m_views.front().surface);
    }

    m_views.insert(m_views.begin() + static_cast<std::ptrdiff_t>(index), std::move(view));
    createdView = true;
  }

  if (createdView) {
    if (m_client->flush() < 0) {
      kLog.error("Wayland flush failed while creating greeter windows");
      m_syncingOutputWindows = false;
      return;
    }
    if (wl_display_roundtrip(m_client->display()) < 0) {
      logWaylandDispatchError(m_client->display(), "roundtrip after createSurface");
      m_syncingOutputWindows = false;
      return;
    }
    if (m_pendingOutputSync) {
      m_syncingOutputWindows = false;
      syncOutputWindows();
      return;
    }
  }

  for (std::size_t i = 0; i < targets.size(); ++i) {
    m_views[i].window->bindOutput(targets[i].output);
    m_views[i].surface->setBoundOutputName(targets[i].name);
    m_views[i].window->matchOutputLogicalSize();
    if (m_sceneReady) {
      m_views[i].window->setSceneReady(true);
    }
  }

  if (m_activeSurface == nullptr && !m_views.empty()) {
    m_activeSurface = m_views.front().surface.get();
  }

  GreeterSurface* keyboardSurface = m_activeSurface;
  if (keyboardSurface == nullptr && !m_views.empty()) {
    keyboardSurface = m_views.front().surface.get();
  }
  setActiveSurface(keyboardSurface);

  m_syncingOutputWindows = false;
  if (m_pendingOutputSync) {
    syncOutputWindows();
  }
}

void Greeter::setActiveSurface(GreeterSurface* surface) {
  m_activeSurface = surface;
  for (auto& view : m_views) {
    view.surface->setKeyboardOwner(view.surface.get() == surface);
  }
}

void Greeter::syncStateFrom(const GreeterSurface* source) {
  if (source == nullptr) {
    return;
  }
  for (auto& view : m_views) {
    if (view.surface.get() != source) {
      view.surface->mirrorStateFrom(*source);
    }
  }
}

Greeter::View* Greeter::viewForWindow(GreeterWindow& window) noexcept {
  for (auto& view : m_views) {
    if (view.window.get() == &window) {
      return &view;
    }
  }
  return nullptr;
}

Greeter::View* Greeter::viewForSurface(wl_surface* surface) noexcept {
  if (surface == nullptr) {
    return nullptr;
  }
  for (auto& view : m_views) {
    if (view.window->wlSurface() == surface) {
      return &view;
    }
  }
  return nullptr;
}

void Greeter::connectGreetd() {
  const char* sockPath = std::getenv("GREETD_SOCK");
  std::string path = sockPath ? sockPath : "/run/greetd/server.sock";

  if (!m_greetdClient.connect(path)) {
    kLog.error("failed to connect to greetd at {}", path);
    m_greetdUnavailable = true;
  }
}

void Greeter::onGreetdTimeout(const GreetdRequestTimeout& timeout) {
  kLog.error(
      "greetd request '{}' timed out after {} ms", greetdRequestTypeName(timeout.request), timeout.elapsed.count()
  );

  // The greetd auth worker may have died while its parent is still holding the
  // session lock. Do not send cancel, reconnect, or exit: none can recover that
  // daemon state, and exiting would leave the VT without a greeter.
  markGreetdUnavailable("Login service stopped responding. Restart greetd.");
}

void Greeter::onGreetdTransportError(const GreetdError& error) {
  kLog.error("greetd transport failed: {}", error.description);
  markGreetdUnavailable("Login service connection failed. Restart greetd.");
}

void Greeter::markGreetdUnavailable(const std::string_view reason) {
  m_greetdClient.disconnect();
  m_greetdUnavailable = true;
  m_authSurface = nullptr;
  for (auto& view : m_views) {
    if (view.surface) {
      view.surface->setGreetdUnavailable(reason);
    }
  }
}

bool Greeter::claimAuthSurface(GreeterSurface* surface) {
  if (surface == nullptr || m_greetdUnavailable) {
    return false;
  }
  if (m_authSurface != nullptr && m_authSurface != surface) {
    return false;
  }
  m_authSurface = surface;
  for (auto& view : m_views) {
    view.surface->setSharedAuthBlocked(view.surface.get() != surface);
  }
  return true;
}

void Greeter::releaseAuthSurface(GreeterSurface* surface) {
  if (surface == nullptr || m_authSurface != surface) {
    return;
  }
  m_authSurface = nullptr;
  for (auto& view : m_views) {
    view.surface->setSharedAuthBlocked(false);
  }
}

void Greeter::onGreetdReadable() {
  // The owner persists through cancellation, even after authInProgress() is
  // cleared, so its final ack is still correlated with the right surface.
  GreeterSurface* target = m_authSurface;
  if (target == nullptr) {
    target = m_activeSurface;
  }
  if (target == nullptr && !m_views.empty()) {
    target = m_views.front().surface.get();
  }
  if (target != nullptr) {
    target->onGreetdReadable();
  }
}

void Greeter::setupInputCallbacks(WaylandClient& client) {
  client.setPointerEventCallback([this](const PointerEvent& event) {
    View* view = viewForSurface(event.surface);
    if (view == nullptr) {
      return;
    }

    switch (event.type) {
    case PointerEvent::Type::Enter:
      setActiveSurface(view->surface.get());
      onPointerMotion(*view->window, event.sx, event.sy);
      break;
    case PointerEvent::Type::Leave:
      onPointerLeave(*view->window);
      break;
    case PointerEvent::Type::Motion:
      setActiveSurface(view->surface.get());
      onPointerMotion(*view->window, event.sx, event.sy);
      break;
    case PointerEvent::Type::Button:
      setActiveSurface(view->surface.get());
      onPointerMotion(*view->window, event.sx, event.sy);
      onPointerButton(*view->window, event.sx, event.sy, event.button, event.state != 0);
      break;
    case PointerEvent::Type::Axis:
      setActiveSurface(view->surface.get());
      onPointerAxis(*view->window, event.sx, event.sy, event.axis, event.axisLines);
      break;
    default:
      break;
    }
  });

  client.setKeyboardEventCallback([this](const KeyboardEvent& event) {
    onKeyboardEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
  });
}

void Greeter::onKeyboardEvent(
    std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit
) {
  GreeterSurface* surface = m_activeSurface;
  if (surface == nullptr && !m_views.empty()) {
    surface = m_views.front().surface.get();
  }
  if (surface != nullptr) {
    surface->onKeyEvent(sym, utf32, modifiers, pressed, preedit);
  }
}

void Greeter::onPointerLeave(GreeterWindow& window) {
  if (View* view = viewForWindow(window)) {
    view->surface->onPointerLeave();
  }
}

void Greeter::onPointerMotion(GreeterWindow& window, double x, double y) {
  if (View* view = viewForWindow(window)) {
    view->surface->onPointerMotion(static_cast<float>(x), static_cast<float>(y));
  }
}

void Greeter::onPointerButton(GreeterWindow& window, double x, double y, std::uint32_t button, bool pressed) {
  if (View* view = viewForWindow(window)) {
    view->surface->onPointerEvent(static_cast<float>(x), static_cast<float>(y), button, pressed);
  }
}

void Greeter::onPointerAxis(GreeterWindow& window, double x, double y, std::uint32_t axis, float axisLines) {
  if (View* view = viewForWindow(window)) {
    view->surface->onPointerAxis(static_cast<float>(x), static_cast<float>(y), axis, axisLines);
  }
}

void Greeter::onThemeChanged() {
  for (auto& view : m_views) {
    view.surface->onThemeChanged();
  }
}
