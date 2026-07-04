#include "greeter/greeter.h"

#include "core/log.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/logind_resume.h"
#include "render/render_context.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/input.h"
#include "wayland/wayland_client.h"
#include "wayland/wayland_seat.h"

#include <cerrno>
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

  const greeter::GreeterPreferences prefs = greeter::loadGreeterPreferences();

  Input::setPasswordMaskStyle(
      prefs.passwordMaskStyle == greeter::PasswordMaskStyle::RandomIcons ? Input::PasswordMaskStyle::RandomIcons
                                                                         : Input::PasswordMaskStyle::CircleFilled
  );

  GlyphRegistry::initialize();

  m_glSharedContext.initialize(client.display());

  m_renderContext = std::make_unique<RenderContext>();
  m_renderContext->initialize(m_glSharedContext);

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
    const int timeoutMs = repeatMs >= 0 ? repeatMs : -1;

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

    if (wl_display_dispatch_pending(display) < 0) {
      logWaylandDispatchError(display, "dispatch_pending");
      return 1;
    }
  }

  return 0;
}

void Greeter::syncOutputWindows() {
  if (m_client == nullptr || m_syncingOutputWindows) {
    return;
  }
  m_syncingOutputWindows = true;

  const auto targets = m_client->greeterTargetOutputs();
  kLog.info("syncOutputWindows: {} target output(s), {} view(s)", targets.size(), m_views.size());

  while (m_views.size() > targets.size()) {
    if (m_activeSurface == m_views.back().surface.get()) {
      m_activeSurface = m_views.empty() ? nullptr : m_views.front().surface.get();
    }
    m_views.pop_back();
  }

  const std::size_t viewsBefore = m_views.size();
  while (m_views.size() < targets.size()) {
    View view;
    view.surface = std::make_unique<GreeterSurface>();
    view.surface->setGreetdClient(&m_greetdClient);
    view.surface->setOnExitRequested([this]() { m_exitRequested = true; });
    view.surface->setOnStateChanged([this](GreeterSurface* source) { syncStateFrom(source); });
    view.surface->initialize(m_renderContext.get());

    view.window = std::make_unique<GreeterWindow>(*m_client, m_glSharedContext, *m_renderContext, *view.surface);
    view.surface->setWindow(view.window.get());

    if (!view.window->createSurface()) {
      kLog.error("failed to create greeter window");
      m_syncingOutputWindows = false;
      return;
    }

    const std::size_t index = m_views.size();
    view.window->bindOutput(targets[index]->output);
    kLog.info("greeter view for output '{}'", targets[index]->name.empty() ? "?" : targets[index]->name.c_str());

    if (!m_views.empty()) {
      view.surface->mirrorStateFrom(*m_views.front().surface);
    }

    m_views.push_back(std::move(view));
  }

  if (m_views.size() > viewsBefore) {
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
  }

  for (std::size_t i = 0; i < targets.size(); ++i) {
    m_views[i].window->bindOutput(targets[i]->output);
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
  }
}

void Greeter::onGreetdReadable() {
  // Route replies to the authenticating surface; fall back to the active (or
  // first) surface to drain stray acks.
  GreeterSurface* target = nullptr;
  for (auto& view : m_views) {
    if (view.surface && view.surface->authInProgress()) {
      target = view.surface.get();
      break;
    }
  }
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
