#include "greeter/greeter.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/library_runtime.h"
#include "greeter/logind_resume.h"
#include "greeter/resources.h"
#include "render/render_context.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/input.h"
#include "wayland/output_span_layout.h"
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

    kLog.info("preferred output: {}", *configured);
  }
} // namespace

Greeter::Greeter() : m_authController(m_model, m_greetdClient) {}

Greeter::~Greeter() = default;

bool Greeter::initialize(WaylandClient& client) {
  m_client = &client;
  m_initializing = true;

  greeter::config::clearConfigDiagnostics();
  m_model.initialize();
  m_model.setOnChanged([this](GreeterViewId source, bool selection) { syncModel(source, selection); });
  m_authController.setOnExitRequested([this]() { m_exitRequested = true; });

  const greeter::GreeterPreferences prefs = greeter::loadGreeterPreferences();

  m_greetdClient.setRequestTimeout(std::chrono::seconds(prefs.authRequestTimeoutSec));

  Input::setPasswordMaskStyle(
      prefs.passwordMaskStyle == greeter::PasswordMaskStyle::RandomIcons ? Input::PasswordMaskStyle::RandomIcons
                                                                         : Input::PasswordMaskStyle::CircleFilled
  );

  initializeGreeterAssets();

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
    drainGreeterUiWork();
    client.repeatTick();

    for (auto& view : m_views) {
      if (view.surface->updateClock()) {
        view.surface->requestLayout();
      }
    }

    if (client.flush() < 0) {
      kLog.error("Wayland flush failed");
      return 1;
    }

    const int repeatMs = client.repeatPollTimeoutMs();
    const int requestMs = m_greetdClient.requestPollTimeoutMs();
    const int eventTimeoutMs = repeatMs < 0 ? requestMs : requestMs < 0 ? repeatMs : std::min(repeatMs, requestMs);
    const auto wallNow = std::chrono::system_clock::now().time_since_epoch();
    const auto wallMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(wallNow).count();
    const auto currentSecondRemainder = (wallMilliseconds % 1000 + 1000) % 1000;
    const int clockTimeoutMs = 1000 - static_cast<int>(currentSecondRemainder);
    const int timeoutMs = eventTimeoutMs < 0 ? clockTimeoutMs : std::min(eventTimeoutMs, clockTimeoutMs);

    while (wl_display_prepare_read(display) != 0) {
      drainGreeterUiWork();
      if (wl_display_dispatch_pending(display) < 0) {
        logWaylandDispatchError(display, "dispatch_pending");
        return 1;
      }
    }

    GPollFD glibPoll{.fd = -1, .events = 0, .revents = 0};
    int glibPriority = 0;
    const int uiTimeout = greeterUiPollTimeoutMs();
    int pollTimeout = uiTimeout < 0 ? timeoutMs : std::min(timeoutMs, uiTimeout);
    if (resumeMonitor.active()) {
      resumeMonitor.prepareDispatch(glibPriority, glibPoll, pollTimeout);
    }

    pollfd pfds[4]{};
    pfds[0].fd = wl_display_get_fd(display);
    pfds[0].events = POLLIN;
    int pollCount = 1;

    pfds[pollCount].fd = DeferredCall::wakeFd();
    pfds[pollCount].events = POLLIN;
    ++pollCount;

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
        m_authController.onGreetdReadable();
      }
    } else {
      wl_display_cancel_read(display);
    }

    // A readable reply at the deadline wins because it was drained above and
    // disarmed the watchdog before this check.
    if (const auto timeout = m_greetdClient.timedOutRequest()) {
      m_authController.onGreetdTimeout(*timeout);
    }

    drainGreeterUiWork();
    if (wl_display_dispatch_pending(display) < 0) {
      logWaylandDispatchError(display, "dispatch_pending");
      return 1;
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
    std::string identifier;
    std::optional<WaylandOutputLayout> layout;
  };
  std::vector<TargetOutput> targets;
  for (const WaylandOutputInfo* output : m_client->greeterTargetOutputs()) {
    if (output != nullptr) {
      targets.push_back(
          TargetOutput{
              .output = output->output,
              .name = output->name,
              .identifier = output->stableIdentifier(),
              .layout = m_client->logicalLayoutForOutput(output->output),
          }
      );
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
    m_authController.onViewRemoved(it->surface->viewId());
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
    view.surface = std::make_unique<GreeterSurface>(m_model, m_authController, m_nextViewId++);
    view.surface->initialize(m_renderContext.get());

    view.window = std::make_unique<GreeterWindow>(*m_client, m_glSharedContext, *m_renderContext, *view.surface);
    view.surface->setWindow(view.window.get());

    view.window->bindOutput(targets[index].output);
    if (!view.window->createSurface()) {
      kLog.error("failed to create greeter window");
      m_syncingOutputWindows = false;
      return;
    }

    view.window->bindOutput(targets[index].output);
    view.surface->setBoundOutputName(targets[index].name);
    view.output = targets[index].output;
    kLog.info("greeter view for output '{}'", targets[index].name.empty() ? "?" : targets[index].name.c_str());

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

  std::vector<greeter::OutputSpanGeometry> spanOutputs;
  if (targets.size() > 1) {
    spanOutputs.reserve(targets.size());
    for (const TargetOutput& target : targets) {
      if (!target.layout.has_value()) {
        spanOutputs.clear();
        break;
      }
      spanOutputs.push_back(
          greeter::OutputSpanGeometry{
              .x = target.layout->x,
              .y = target.layout->y,
              .width = target.layout->width,
              .height = target.layout->height,
          }
      );
    }
  }

  for (std::size_t i = 0; i < targets.size(); ++i) {
    m_views[i].window->bindOutput(targets[i].output);
    m_views[i].surface->setBoundOutputName(targets[i].name);
    m_views[i].surface->setOutputIdentifier(targets[i].identifier);
    m_views[i].surface->setKeyboardLayoutName(m_client->connection().currentKeyboardLayoutName());
    m_views[i].surface->setWallpaperSpanParams(greeter::computeWallpaperSpanParams(spanOutputs, i));
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

void Greeter::syncModel(GreeterViewId source, bool selection) {
  for (auto& view : m_views) {
    view.surface->syncModel(selection && view.surface->viewId() != source);
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
    m_authController.setUnavailable("Login service is unavailable. Restart greetd.");
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
      onPointerButton(*view->window, event.sx, event.sy, event.button, event.pressed);
      break;
    case PointerEvent::Type::Axis:
      setActiveSurface(view->surface.get());
      onPointerAxis(*view->window, event.sx, event.sy, event.axis, event.axisLines);
      break;
    default:
      break;
    }
  });

  client.connection().setKeyboardLayoutChangeCallback([this] {
    for (auto& view : m_views)
      view.surface->setKeyboardLayoutName(m_client->connection().currentKeyboardLayoutName());
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
