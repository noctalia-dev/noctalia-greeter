#pragma once

#include "greetd/greetd_client.h"
#include "render/gl_shared_context.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GreeterSurface;
class GreeterWindow;
class RenderContext;
class WaylandClient;
struct wl_surface;

class Greeter {
public:
  Greeter();
  ~Greeter();

  bool initialize(WaylandClient& client);
  int run(WaylandClient& client, const std::atomic<bool>& shutdownRequested);

  void onKeyboardEvent(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit);
  void onPointerLeave(GreeterWindow& window);
  void onPointerMotion(GreeterWindow& window, double x, double y);
  void onPointerButton(GreeterWindow& window, double x, double y, std::uint32_t button, bool pressed);
  void onPointerAxis(GreeterWindow& window, double x, double y, std::uint32_t axis, float axisLines);

  void onThemeChanged();

private:
  struct View {
    std::unique_ptr<GreeterSurface> surface;
    std::unique_ptr<GreeterWindow> window;
  };

  void connectGreetd();
  void onGreetdReadable();
  void setupInputCallbacks(WaylandClient& client);
  void syncOutputWindows();
  void syncStateFrom(const GreeterSurface* source);
  void setActiveSurface(GreeterSurface* surface);
  [[nodiscard]] View* viewForWindow(GreeterWindow& window) noexcept;
  [[nodiscard]] View* viewForSurface(wl_surface* surface) noexcept;

  WaylandClient* m_client = nullptr;
  GlSharedContext m_glSharedContext;
  std::unique_ptr<RenderContext> m_renderContext;
  std::vector<View> m_views;
  GreetdClient m_greetdClient;
  GreeterSurface* m_activeSurface = nullptr;

  std::string m_defaultUsername;
  bool m_exitRequested = false;
  bool m_sceneReady = false;
  bool m_initializing = false;
  bool m_pendingOutputSync = false;
  bool m_syncingOutputWindows = false;
};
