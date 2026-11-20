#pragma once

#include "greeter/greeter_preferences.h"
#include "wayland/wayland_connection.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_registry;
struct wl_seat;
struct wp_fractional_scale_manager_v1;
struct wp_viewporter;
struct xdg_wm_base;

struct WaylandOutputLayout {
  int32_t x = 0;
  int32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct WaylandOutputInfo {
  wl_output* output = nullptr;
  std::uint32_t registryName = 0;
  std::string name;
  std::string make;
  std::string model;
  std::string description;
  int32_t x = 0;
  int32_t y = 0;
  int32_t pixelWidth = 0;
  int32_t pixelHeight = 0;
  int32_t scale = 1;
  int32_t transform = 0;
  float preferredScale = 0.0f;
  bool done = false;

  [[nodiscard]] std::string stableIdentifier() const;
  [[nodiscard]] bool matchesIdentifier(std::string_view identifier) const;
};

class WaylandClient {
public:
  WaylandClient() = default;
  ~WaylandClient() = default;

  WaylandClient(const WaylandClient&) = delete;
  WaylandClient& operator=(const WaylandClient&) = delete;

  bool connect();

  [[nodiscard]] int dispatch() const;
  [[nodiscard]] int dispatchPending() const;
  [[nodiscard]] int flush() const;

  void setPointerEventCallback(WaylandSeat::PointerEventCallback callback);
  void setKeyboardEventCallback(WaylandSeat::KeyboardEventCallback callback);
  void setOutputsChangedCallback(std::function<void()> callback);
  void setPreferredOutputName(std::optional<std::string> name);

  [[nodiscard]] int repeatPollTimeoutMs() const;
  void repeatTick();

  [[nodiscard]] WaylandConnection& connection() noexcept { return m_connection; }
  [[nodiscard]] wl_display* display() const noexcept { return m_connection.display(); }
  [[nodiscard]] wl_compositor* compositor() const noexcept { return m_connection.compositor(); }
  [[nodiscard]] wl_seat* seat() const noexcept { return m_connection.seat(); }
  [[nodiscard]] xdg_wm_base* xdgWmBase() const noexcept { return m_connection.xdgWmBase(); }
  [[nodiscard]] bool hasXdgShell() const noexcept { return m_connection.hasXdgShell(); }
  [[nodiscard]] const std::vector<WaylandOutputInfo>& outputs() const noexcept { return m_outputs; }

  // Buffer scale for wl_surface_set_buffer_scale and HiDPI buffer sizing.
  [[nodiscard]] int32_t effectiveBufferScale() const noexcept;
  [[nodiscard]] int32_t preferredBufferScale() const noexcept;
  [[nodiscard]] int32_t outputBufferScale(const wl_output* output) const noexcept;
  // Largest single output (logical pixels).
  [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> primaryLogicalSize() const noexcept;
  [[nodiscard]] std::optional<std::string> primaryConnectorName() const noexcept;
  [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> preferredLogicalSize() const noexcept;
  [[nodiscard]] bool hasPreferredOutputName() const noexcept;
  [[nodiscard]] bool hasReadyOutputs() const noexcept;
  [[nodiscard]] std::vector<const WaylandOutputInfo*> readyOutputsSorted() const noexcept;
  [[nodiscard]] std::vector<const WaylandOutputInfo*> greeterTargetOutputs() const noexcept;
  [[nodiscard]] bool hasResolvedPreferredOutput() const noexcept;
  void forgetPreferredOutput() noexcept;
  [[nodiscard]] std::optional<WaylandOutputLayout> preferredOutputLayout() const noexcept;
  [[nodiscard]] std::optional<WaylandOutputLayout> greeterOutputLayout() const noexcept;
  [[nodiscard]] bool needsOutputViewport() const noexcept;
  [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> targetLogicalSize() const noexcept;
  [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> combinedLogicalSize() const noexcept;
  [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
  logicalSizeForOutput(const wl_output* output) const noexcept;
  [[nodiscard]] std::optional<WaylandOutputLayout> logicalLayoutForOutput(const wl_output* output) const noexcept;
  void setOutputPreferredScale(wl_output* output, float scale) noexcept;

private:
  void refreshOutputs();
  [[nodiscard]] const WaylandOutputInfo* primaryOutput() const noexcept;
  [[nodiscard]] const WaylandOutputInfo* preferredOutput() const noexcept;
  [[nodiscard]] const WaylandOutputInfo* findOutputByName(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<WaylandOutputLayout> layoutForOutput(const WaylandOutputInfo& output) const noexcept;
  void notifyOutputsChanged();

  std::optional<std::string> m_preferredOutputName;
  WaylandConnection m_connection;
  std::vector<WaylandOutputInfo> m_outputs;
  std::vector<greeter::GreeterOutputPlacement> m_outputLayout;
  std::function<void()> m_outputsChangedCallback;
};
