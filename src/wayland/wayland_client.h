#pragma once

#include "greeter/greeter_preferences.h"
#include "wayland/wayland_seat.h"

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
struct zwlr_output_head_v1;
struct zwlr_output_manager_v1;
struct zwlr_output_mode_v1;

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
  std::string description;
  std::string make;
  std::string model;
  std::string serial;
  int32_t x = 0;
  int32_t y = 0;
  int32_t physicalWidthMm = 0;
  int32_t physicalHeightMm = 0;
  int32_t pixelWidth = 0;
  int32_t pixelHeight = 0;
  int32_t scale = 1;
  float preferredScale = 0.0f;
  bool done = false;
};

class WaylandClient {
public:
  WaylandClient();
  ~WaylandClient();

  WaylandClient(const WaylandClient&) = delete;
  WaylandClient& operator=(const WaylandClient&) = delete;

  bool connect();
  void disconnect();

  [[nodiscard]] int dispatch() const;
  [[nodiscard]] int dispatchPending() const;
  [[nodiscard]] int flush() const;

  void setPointerEventCallback(WaylandSeat::PointerEventCallback callback);
  void setKeyboardEventCallback(WaylandSeat::KeyboardEventCallback callback);
  void setOutputsChangedCallback(std::function<void()> callback);
  void setPreferredOutputName(std::optional<std::string> name);

  [[nodiscard]] int repeatPollTimeoutMs() const;
  void repeatTick();

  [[nodiscard]] wl_display* display() const noexcept { return m_display; }
  [[nodiscard]] wl_compositor* compositor() const noexcept { return m_compositor; }
  [[nodiscard]] wl_seat* seat() const noexcept { return m_seat; }
  [[nodiscard]] xdg_wm_base* xdgWmBase() const noexcept { return m_xdgWmBase; }
  [[nodiscard]] wp_fractional_scale_manager_v1* fractionalScaleManager() const noexcept {
    return m_fractionalScaleManager;
  }
  [[nodiscard]] wp_viewporter* viewporter() const noexcept { return m_viewporter; }
  [[nodiscard]] bool hasXdgShell() const noexcept { return m_xdgWmBase != nullptr; }
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
  void setOutputPreferredScale(wl_output* output, float scale) noexcept;

  static void
  handleGlobal(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version);
  static void handleGlobalRemove(void* data, wl_registry* registry, std::uint32_t name);
  static void handleOutputMode(
      void* data, wl_output* output, std::uint32_t flags, std::int32_t width, std::int32_t height, std::int32_t refresh
  );
  static void handleOutputGeometry(
      void* data, wl_output* output, std::int32_t x, std::int32_t y, std::int32_t physWidth, std::int32_t physHeight,
      std::int32_t subpixel, const char* make, const char* model, std::int32_t transform
  );
  static void handleOutputDone(void* data, wl_output* output);
  static void handleOutputScale(void* data, wl_output* output, std::int32_t factor);
  static void handleOutputName(void* data, wl_output* output, const char* name);
  static void handleOutputDescription(void* data, wl_output* output, const char* description);
  static void handleOutputManagerHead(void* data, zwlr_output_manager_v1* manager, zwlr_output_head_v1* head);
  static void handleOutputManagerDone(void* data, zwlr_output_manager_v1* manager, std::uint32_t serial);
  static void handleOutputManagerFinished(void* data, zwlr_output_manager_v1* manager);
  static void handleOutputHeadName(void* data, zwlr_output_head_v1* head, const char* name);
  static void handleOutputHeadDescription(void* data, zwlr_output_head_v1* head, const char* description);
  static void handleOutputHeadMake(void* data, zwlr_output_head_v1* head, const char* make);
  static void handleOutputHeadModel(void* data, zwlr_output_head_v1* head, const char* model);
  static void handleOutputHeadSerial(void* data, zwlr_output_head_v1* head, const char* serial);
  static void handleOutputHeadFinished(void* data, zwlr_output_head_v1* head);
  static void handleOutputModeFinished(void* data, zwlr_output_mode_v1* mode);

private:
  void bindGlobal(wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version);
  void bindOutput(wl_registry* registry, std::uint32_t name, std::uint32_t version);
  void matchPendingOutputHeads();
  [[nodiscard]] const WaylandOutputInfo* primaryOutput() const noexcept;
  [[nodiscard]] const WaylandOutputInfo* preferredOutput() const noexcept;
  [[nodiscard]] const WaylandOutputInfo* findOutputByName(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<WaylandOutputLayout> layoutForOutput(const WaylandOutputInfo& output) const noexcept;
  void notifyOutputsChanged();

  std::optional<std::string> m_preferredOutputName;
  wl_display* m_display = nullptr;
  wl_registry* m_registry = nullptr;
  wl_compositor* m_compositor = nullptr;
  wl_seat* m_seat = nullptr;
  xdg_wm_base* m_xdgWmBase = nullptr;
  wp_fractional_scale_manager_v1* m_fractionalScaleManager = nullptr;
  wp_viewporter* m_viewporter = nullptr;
  zwlr_output_manager_v1* m_outputManager = nullptr;
  struct PendingOutputHead {
    zwlr_output_head_v1* head = nullptr;
    std::string name;
    std::string description;
    std::string make;
    std::string model;
    std::string serial;
  };
  std::vector<PendingOutputHead> m_pendingOutputHeads;
  WaylandSeat m_seatHandler;
  std::vector<WaylandOutputInfo> m_outputs;
  std::vector<greeter::GreeterOutputPlacement> m_outputLayout;
  std::function<void()> m_outputsChangedCallback;
};
