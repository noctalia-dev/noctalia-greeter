#include "wayland/wayland_client.h"

#include "core/log.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>

namespace {
constexpr Logger kLog("wayland");

void xdgWmBasePing(void * /*data*/, xdg_wm_base *wmBase, std::uint32_t serial) {
  xdg_wm_base_pong(wmBase, serial);
}

const xdg_wm_base_listener kXdgWmBaseListener = {
    .ping = xdgWmBasePing,
};

const wl_registry_listener kRegistryListener = {
    .global = &WaylandClient::handleGlobal,
    .global_remove = &WaylandClient::handleGlobalRemove,
};

[[nodiscard]] float outputScaleFactor(const WaylandOutputInfo &out) noexcept {
  if (out.preferredScale > 0.0f) {
    return out.preferredScale;
  }
  return static_cast<float>(std::max(1, out.scale));
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
logicalSizeForOutputInfo(const WaylandOutputInfo &out) {
  if (!out.done || out.pixelWidth <= 0 || out.pixelHeight <= 0) {
    return std::nullopt;
  }

  const float scale = outputScaleFactor(out);
  if (scale <= 1.01f) {
    return std::pair{static_cast<std::uint32_t>(out.pixelWidth),
                     static_cast<std::uint32_t>(out.pixelHeight)};
  }

  const auto logicalWidth = static_cast<std::uint32_t>(
      std::max(1, static_cast<int32_t>(std::lround(
                      static_cast<float>(out.pixelWidth) / scale))));
  const auto logicalHeight = static_cast<std::uint32_t>(
      std::max(1, static_cast<int32_t>(std::lround(
                      static_cast<float>(out.pixelHeight) / scale))));
  return std::pair{logicalWidth, logicalHeight};
}

[[nodiscard]] bool
allReadyOutputsShareOrigin(const std::vector<WaylandOutputInfo> &outputs) {
  std::size_t readyCount = 0;
  for (const auto &out : outputs) {
    if (!out.done || out.pixelWidth <= 0 || out.pixelHeight <= 0) {
      continue;
    }
    ++readyCount;
    if (out.x != 0 || out.y != 0) {
      return false;
    }
  }
  return readyCount > 1;
}

[[nodiscard]] std::size_t
readyOutputCount(const std::vector<WaylandOutputInfo> &outputs) {
  std::size_t count = 0;
  for (const auto &out : outputs) {
    if (out.done && out.pixelWidth > 0 && out.pixelHeight > 0) {
      ++count;
    }
  }
  return count;
}

const wl_output_listener kOutputListener = {
    .geometry = &WaylandClient::handleOutputGeometry,
    .mode = &WaylandClient::handleOutputMode,
    .done = &WaylandClient::handleOutputDone,
    .scale = &WaylandClient::handleOutputScale,
    .name = &WaylandClient::handleOutputName,
    .description = &WaylandClient::handleOutputDescription,
};

void logWaylandConnectDiagnostics() {
  const char *wlDisplay = std::getenv("WAYLAND_DISPLAY");
  const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
  if (runtimeDir == nullptr || runtimeDir[0] == '\0') {
    kLog.warn("XDG_RUNTIME_DIR is unset");
    return;
  }

  struct stat runtimeStat{};
  if (stat(runtimeDir, &runtimeStat) != 0) {
    kLog.warn("XDG_RUNTIME_DIR missing or inaccessible: {} (errno={} '{}')",
              runtimeDir, errno, std::strerror(errno));
  } else if (!S_ISDIR(runtimeStat.st_mode)) {
    kLog.warn("XDG_RUNTIME_DIR is not a directory: {}", runtimeDir);
  }

  if (wlDisplay == nullptr || wlDisplay[0] == '\0') {
    kLog.warn("WAYLAND_DISPLAY is unset");
    return;
  }

  const std::string socketPath = std::string(runtimeDir) + "/" + wlDisplay;
  struct stat socketStat{};
  if (stat(socketPath.c_str(), &socketStat) == 0) {
    kLog.info("Wayland socket present: {}", socketPath);
    return;
  }

  kLog.warn("Wayland socket missing: {} (errno={} '{}')", socketPath, errno,
            std::strerror(errno));

  DIR *dir = opendir(runtimeDir);
  if (dir == nullptr) {
    kLog.warn("cannot list {} (errno={} '{}')", runtimeDir, errno,
              std::strerror(errno));
    return;
  }

  std::string entries;
  while (dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    if (!entries.empty()) {
      entries += ", ";
    }
    entries += entry->d_name;
  }
  closedir(dir);
  kLog.warn("XDG_RUNTIME_DIR contents: {}",
            entries.empty() ? "(empty)" : entries);
}
} // namespace

WaylandClient::WaylandClient() = default;

WaylandClient::~WaylandClient() { disconnect(); }

bool WaylandClient::connect() {
  if (m_display != nullptr) {
    return true;
  }

  constexpr int kMaxConnectAttempts = 60; // ~3s total with 50ms backoff
  for (int attempt = 1; attempt <= kMaxConnectAttempts; ++attempt) {
    errno = 0;
    m_display = wl_display_connect(nullptr);
    if (m_display != nullptr) {
      break;
    }

    const int err = errno;
    const bool retryable =
        (err == ENOENT || err == ECONNREFUSED || err == EACCES);
    if (!retryable || attempt == kMaxConnectAttempts) {
      const char *wlDisplay = std::getenv("WAYLAND_DISPLAY");
      const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
      kLog.error("wl_display_connect failed (errno={} '{}', "
                 "WAYLAND_DISPLAY={}, XDG_RUNTIME_DIR={}, attempt={}/{})",
                 err, std::strerror(err),
                 wlDisplay != nullptr ? wlDisplay : "unset",
                 runtimeDir != nullptr ? runtimeDir : "unset", attempt,
                 kMaxConnectAttempts);
      logWaylandConnectDiagnostics();
      return false;
    }

    if (attempt == 1) {
      logWaylandConnectDiagnostics();
    }

    if (attempt == 1 || attempt % 10 == 0) {
      kLog.warn("wl_display_connect retry {}/{} (errno={} '{}')", attempt,
                kMaxConnectAttempts, err, std::strerror(err));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  m_registry = wl_display_get_registry(m_display);
  if (m_registry == nullptr) {
    kLog.error("wl_display_get_registry failed");
    disconnect();
    return false;
  }

  if (wl_registry_add_listener(m_registry, &kRegistryListener, this) != 0) {
    kLog.error("wl_registry_add_listener failed");
    disconnect();
    return false;
  }

  if (wl_display_roundtrip(m_display) < 0) {
    kLog.error("Wayland registry roundtrip failed: {}", std::strerror(errno));
    disconnect();
    return false;
  }

  if (!m_compositor || !m_seat || !m_xdgWmBase) {
    kLog.error("missing required Wayland globals (compositor/seat/xdg-shell)");
    disconnect();
    return false;
  }

  kLog.info("connected to Wayland compositor");
  return true;
}

void WaylandClient::disconnect() {
  m_seatHandler.cleanup();

  if (m_xdgWmBase != nullptr) {
    xdg_wm_base_destroy(m_xdgWmBase);
    m_xdgWmBase = nullptr;
  }
  if (m_seat != nullptr) {
    wl_seat_destroy(m_seat);
    m_seat = nullptr;
  }
  if (m_compositor != nullptr) {
    wl_compositor_destroy(m_compositor);
    m_compositor = nullptr;
  }
  if (m_registry != nullptr) {
    wl_registry_destroy(m_registry);
    m_registry = nullptr;
  }
  if (m_display != nullptr) {
    wl_display_disconnect(m_display);
    m_display = nullptr;
  }
}

int WaylandClient::dispatch() const {
  return m_display != nullptr ? wl_display_dispatch(m_display) : -1;
}

int WaylandClient::dispatchPending() const {
  return m_display != nullptr ? wl_display_dispatch_pending(m_display) : -1;
}

int WaylandClient::flush() const {
  return m_display != nullptr ? wl_display_flush(m_display) : -1;
}

void WaylandClient::setPointerEventCallback(
    WaylandSeat::PointerEventCallback callback) {
  m_seatHandler.setPointerEventCallback(std::move(callback));
}

void WaylandClient::setKeyboardEventCallback(
    WaylandSeat::KeyboardEventCallback callback) {
  m_seatHandler.setKeyboardEventCallback(std::move(callback));
}

void WaylandClient::setOutputsChangedCallback(std::function<void()> callback) {
  m_outputsChangedCallback = std::move(callback);
}

void WaylandClient::setPreferredOutputName(std::optional<std::string> name) {
  if (name.has_value() && name->empty()) {
    m_preferredOutputName = std::nullopt;
    return;
  }
  m_preferredOutputName = std::move(name);
}

const WaylandOutputInfo *
WaylandClient::findOutputByName(const std::string_view name) const noexcept {
  if (name.empty()) {
    return nullptr;
  }
  for (const auto &out : m_outputs) {
    if (out.done && out.name == name) {
      return &out;
    }
  }
  return nullptr;
}

const WaylandOutputInfo *WaylandClient::preferredOutput() const noexcept {
  if (!hasResolvedPreferredOutput()) {
    return nullptr;
  }
  return findOutputByName(*m_preferredOutputName);
}

const WaylandOutputInfo *WaylandClient::primaryOutput() const noexcept {
  const WaylandOutputInfo *best = nullptr;
  std::int64_t bestArea = 0;
  for (const auto &out : m_outputs) {
    if (!out.done || out.pixelWidth <= 0 || out.pixelHeight <= 0) {
      continue;
    }
    const std::int64_t area = static_cast<std::int64_t>(out.pixelWidth) *
                              static_cast<std::int64_t>(out.pixelHeight);
    if (area > bestArea) {
      bestArea = area;
      best = &out;
    }
  }
  return best;
}

int32_t WaylandClient::effectiveBufferScale() const noexcept {
  int32_t maxScale = 1;
  for (const auto &out : m_outputs) {
    if (!out.done || out.pixelWidth <= 0 || out.pixelHeight <= 0) {
      continue;
    }

    const float scale = outputScaleFactor(out);
    if (scale <= 1.01f) {
      continue;
    }

    maxScale =
        std::max(maxScale, static_cast<int32_t>(std::lround(std::ceil(scale))));
  }

  return maxScale;
}

int32_t WaylandClient::preferredBufferScale() const noexcept {
  const WaylandOutputInfo *out = preferredOutput();
  if (out == nullptr) {
    return effectiveBufferScale();
  }
  return outputBufferScale(out->output);
}

int32_t
WaylandClient::outputBufferScale(const wl_output *output) const noexcept {
  if (output == nullptr) {
    return effectiveBufferScale();
  }

  for (const auto &out : m_outputs) {
    if (out.output != output || !out.done || out.pixelWidth <= 0 ||
        out.pixelHeight <= 0) {
      continue;
    }

    const float scale = outputScaleFactor(out);
    if (scale <= 1.01f) {
      return 1;
    }
    return static_cast<int32_t>(std::lround(std::ceil(scale)));
  }

  return effectiveBufferScale();
}

void WaylandClient::setOutputPreferredScale(wl_output *output,
                                            float scale) noexcept {
  if (output == nullptr || scale <= 0.0f) {
    return;
  }
  for (auto &out : m_outputs) {
    if (out.output != output) {
      continue;
    }
    if (std::fabs(out.preferredScale - scale) < 0.01f) {
      return;
    }
    out.preferredScale = scale;
    if (out.done) {
      notifyOutputsChanged();
    }
    break;
  }
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
WaylandClient::primaryLogicalSize() const noexcept {
  const WaylandOutputInfo *out = primaryOutput();
  if (out == nullptr) {
    return std::nullopt;
  }
  return logicalSizeForOutputInfo(*out);
}

std::optional<std::string>
WaylandClient::primaryConnectorName() const noexcept {
  const WaylandOutputInfo *out = primaryOutput();
  if (out == nullptr || out->name.empty()) {
    return std::nullopt;
  }
  return out->name;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
WaylandClient::preferredLogicalSize() const noexcept {
  const WaylandOutputInfo *out = preferredOutput();
  if (out == nullptr) {
    return std::nullopt;
  }
  return logicalSizeForOutputInfo(*out);
}

bool WaylandClient::hasPreferredOutputName() const noexcept {
  return m_preferredOutputName.has_value() && !m_preferredOutputName->empty();
}

bool WaylandClient::hasReadyOutputs() const noexcept {
  return readyOutputCount(m_outputs) > 0;
}

std::vector<const WaylandOutputInfo *>
WaylandClient::readyOutputsSorted() const noexcept {
  std::vector<const WaylandOutputInfo *> ready;
  ready.reserve(m_outputs.size());
  for (const auto &out : m_outputs) {
    if (out.done && out.pixelWidth > 0 && out.pixelHeight > 0) {
      ready.push_back(&out);
    }
  }
  std::sort(ready.begin(), ready.end(),
            [](const WaylandOutputInfo *lhs, const WaylandOutputInfo *rhs) {
              return lhs->name < rhs->name;
            });
  return ready;
}

std::vector<const WaylandOutputInfo *>
WaylandClient::greeterTargetOutputs() const noexcept {
  if (hasResolvedPreferredOutput()) {
    if (const WaylandOutputInfo *out = preferredOutput()) {
      return {out};
    }
    return {};
  }
  return readyOutputsSorted();
}

bool WaylandClient::hasResolvedPreferredOutput() const noexcept {
  return hasPreferredOutputName() &&
         findOutputByName(*m_preferredOutputName) != nullptr;
}

void WaylandClient::forgetPreferredOutput() noexcept {
  m_preferredOutputName = std::nullopt;
}

std::optional<WaylandOutputLayout>
WaylandClient::layoutForOutput(const WaylandOutputInfo &output) const noexcept {
  const auto logical = logicalSizeForOutputInfo(output);
  if (!logical) {
    return std::nullopt;
  }

  int32_t x = output.x;
  int32_t y = output.y;
  if (allReadyOutputsShareOrigin(m_outputs)) {
    std::vector<const WaylandOutputInfo *> ordered;
    ordered.reserve(m_outputs.size());
    for (const auto &candidate : m_outputs) {
      if (!candidate.done || candidate.pixelWidth <= 0 ||
          candidate.pixelHeight <= 0) {
        continue;
      }
      ordered.push_back(&candidate);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const WaylandOutputInfo *lhs, const WaylandOutputInfo *rhs) {
                return lhs->name < rhs->name;
              });
    x = 0;
    for (const WaylandOutputInfo *candidate : ordered) {
      if (candidate->output == output.output) {
        break;
      }
      if (const auto candidateLogical = logicalSizeForOutputInfo(*candidate)) {
        x += static_cast<int32_t>(candidateLogical->first);
      }
    }
    kLog.info("output '{}' synthetic layout at ({},{}) {}x{} (compositor "
              "reported overlapping origins)",
              output.name.empty() ? "?" : output.name.c_str(), x, y,
              logical->first, logical->second);
  }

  return WaylandOutputLayout{
      .x = x,
      .y = y,
      .width = logical->first,
      .height = logical->second,
  };
}

std::optional<WaylandOutputLayout>
WaylandClient::preferredOutputLayout() const noexcept {
  const WaylandOutputInfo *out = preferredOutput();
  if (out == nullptr) {
    return std::nullopt;
  }
  return layoutForOutput(*out);
}

std::optional<WaylandOutputLayout>
WaylandClient::greeterOutputLayout() const noexcept {
  if (hasResolvedPreferredOutput()) {
    return preferredOutputLayout();
  }
  if (readyOutputCount(m_outputs) <= 1) {
    return std::nullopt;
  }
  const WaylandOutputInfo *out = primaryOutput();
  if (out == nullptr) {
    return std::nullopt;
  }
  return layoutForOutput(*out);
}

bool WaylandClient::needsOutputViewport() const noexcept {
  if (readyOutputCount(m_outputs) <= 1 || !hasPreferredOutputName()) {
    return false;
  }
  const auto layout = greeterOutputLayout();
  const auto combined = combinedLogicalSize();
  if (!layout || !combined) {
    return false;
  }
  return combined->first != layout->width || combined->second != layout->height;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
WaylandClient::combinedLogicalSize() const noexcept {
  std::size_t readyCount = 0;
  bool hasNonOriginPlacement = false;
  for (const auto &out : m_outputs) {
    if (!out.done || out.pixelWidth <= 0 || out.pixelHeight <= 0) {
      continue;
    }
    ++readyCount;
    if (out.x != 0 || out.y != 0) {
      hasNonOriginPlacement = true;
    }
  }

  if (readyCount == 0) {
    return std::nullopt;
  }

  if (readyCount == 1) {
    for (const auto &out : m_outputs) {
      if (out.done && out.pixelWidth > 0 && out.pixelHeight > 0) {
        return logicalSizeForOutputInfo(out);
      }
    }
    return std::nullopt;
  }

  if (!hasNonOriginPlacement) {
    std::uint32_t totalWidth = 0;
    std::uint32_t maxHeight = 0;
    for (const auto &out : m_outputs) {
      const auto logical = logicalSizeForOutputInfo(out);
      if (!logical) {
        continue;
      }
      totalWidth += logical->first;
      maxHeight = std::max(maxHeight, logical->second);
    }
    if (totalWidth > 0 && maxHeight > 0) {
      return std::pair{totalWidth, maxHeight};
    }
  }

  bool any = false;
  std::int64_t minX = 0;
  std::int64_t minY = 0;
  std::int64_t maxX = 0;
  std::int64_t maxY = 0;
  for (const auto &out : m_outputs) {
    const auto layout = layoutForOutput(out);
    if (!layout) {
      continue;
    }

    const std::int64_t x0 = layout->x;
    const std::int64_t y0 = layout->y;
    const std::int64_t x1 = x0 + static_cast<std::int64_t>(layout->width);
    const std::int64_t y1 = y0 + static_cast<std::int64_t>(layout->height);
    if (!any) {
      minX = x0;
      minY = y0;
      maxX = x1;
      maxY = y1;
      any = true;
    } else {
      minX = std::min(minX, x0);
      minY = std::min(minY, y0);
      maxX = std::max(maxX, x1);
      maxY = std::max(maxY, y1);
    }
  }

  if (!any || maxX <= minX || maxY <= minY) {
    return std::nullopt;
  }

  return std::pair{static_cast<std::uint32_t>(maxX - minX),
                   static_cast<std::uint32_t>(maxY - minY)};
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
WaylandClient::targetLogicalSize() const noexcept {
  if (needsOutputViewport()) {
    return combinedLogicalSize();
  }
  if (hasResolvedPreferredOutput()) {
    return preferredLogicalSize();
  }
  return primaryLogicalSize();
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
WaylandClient::logicalSizeForOutput(const wl_output *output) const noexcept {
  if (output == nullptr) {
    return primaryLogicalSize();
  }

  for (const auto &out : m_outputs) {
    if (out.output == output) {
      return logicalSizeForOutputInfo(out);
    }
  }
  return std::nullopt;
}

void WaylandClient::notifyOutputsChanged() {
  if (m_outputsChangedCallback) {
    m_outputsChangedCallback();
  }
}

void WaylandClient::handleOutputGeometry(
    void *data, wl_output *wlOut, std::int32_t x, std::int32_t y,
    std::int32_t physWidth, std::int32_t physHeight, std::int32_t /*subpixel*/,
    const char * /*make*/, const char * /*model*/, std::int32_t /*transform*/) {
  auto *client = static_cast<WaylandClient *>(data);
  for (auto &out : client->m_outputs) {
    if (out.output != wlOut) {
      continue;
    }
    const bool changed = out.x != x || out.y != y ||
                         out.physicalWidthMm != physWidth ||
                         out.physicalHeightMm != physHeight;
    out.x = x;
    out.y = y;
    out.physicalWidthMm = physWidth;
    out.physicalHeightMm = physHeight;
    if (changed && out.done) {
      client->notifyOutputsChanged();
    }
    break;
  }
}

void WaylandClient::handleOutputMode(void *data, wl_output *wlOut,
                                     std::uint32_t flags, std::int32_t w,
                                     std::int32_t h, std::int32_t /*refresh*/) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
    return;
  }
  auto *client = static_cast<WaylandClient *>(data);
  for (auto &out : client->m_outputs) {
    if (out.output == wlOut) {
      out.pixelWidth = w;
      out.pixelHeight = h;
      if (out.done) {
        client->notifyOutputsChanged();
      }
      break;
    }
  }
}

void WaylandClient::handleOutputDone(void *data, wl_output *wlOut) {
  auto *client = static_cast<WaylandClient *>(data);
  for (auto &out : client->m_outputs) {
    if (out.output == wlOut && !out.done) {
      out.done = true;
      kLog.info("output '{}' ready {}x{} at ({},{}) scale={} phys={}x{}mm",
                out.name.empty() ? "?" : out.name.c_str(), out.pixelWidth,
                out.pixelHeight, out.x, out.y, out.scale, out.physicalWidthMm,
                out.physicalHeightMm);
      client->notifyOutputsChanged();
      break;
    }
  }
}

void WaylandClient::handleOutputScale(void *data, wl_output *wlOut,
                                      std::int32_t factor) {
  auto *client = static_cast<WaylandClient *>(data);
  for (auto &out : client->m_outputs) {
    if (out.output == wlOut) {
      const int32_t next = std::max(1, factor);
      if (out.scale != next) {
        out.scale = next;
        if (out.done) {
          kLog.info("output scale updated to {}", next);
          client->notifyOutputsChanged();
        }
      }
      break;
    }
  }
}

void WaylandClient::handleOutputName(void *data, wl_output *wlOut,
                                     const char *name) {
  if (name == nullptr) {
    return;
  }
  auto *client = static_cast<WaylandClient *>(data);
  for (auto &out : client->m_outputs) {
    if (out.output == wlOut) {
      out.name = name;
      break;
    }
  }
}

void WaylandClient::handleOutputDescription(void * /*data*/,
                                            wl_output * /*output*/,
                                            const char * /*description*/) {}

void WaylandClient::bindOutput(wl_registry *registry, std::uint32_t name,
                               std::uint32_t version) {
  const std::uint32_t bindVersion = std::min(version, 4u);
  auto *output = static_cast<wl_output *>(
      wl_registry_bind(registry, name, &wl_output_interface, bindVersion));
  if (output == nullptr) {
    return;
  }

  WaylandOutputInfo info;
  info.output = output;
  info.registryName = name;
  m_outputs.push_back(info);
  wl_output_add_listener(output, &kOutputListener, this);
}

int WaylandClient::repeatPollTimeoutMs() const {
  return m_seatHandler.repeatPollTimeoutMs();
}

void WaylandClient::repeatTick() { m_seatHandler.repeatTick(); }

void WaylandClient::handleGlobal(void *data, wl_registry *registry,
                                 std::uint32_t name, const char *interface,
                                 std::uint32_t version) {
  static_cast<WaylandClient *>(data)->bindGlobal(registry, name, interface,
                                                 version);
}

void WaylandClient::handleGlobalRemove(void * /*data*/,
                                       wl_registry * /*registry*/,
                                       std::uint32_t /*name*/) {}

void WaylandClient::bindGlobal(wl_registry *registry, std::uint32_t name,
                               const char *interface, std::uint32_t version) {
  const std::string interfaceName = interface != nullptr ? interface : "";
  const std::uint32_t bindVersion = std::min(version, 6u);

  if (interfaceName == wl_compositor_interface.name) {
    m_compositor = static_cast<wl_compositor *>(wl_registry_bind(
        registry, name, &wl_compositor_interface, bindVersion));
    return;
  }

  if (interfaceName == wl_seat_interface.name) {
    m_seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, name, &wl_seat_interface, bindVersion));
    m_seatHandler.bind(m_seat);
    return;
  }

  if (interfaceName == xdg_wm_base_interface.name) {
    m_xdgWmBase = static_cast<xdg_wm_base *>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, bindVersion));
    xdg_wm_base_add_listener(m_xdgWmBase, &kXdgWmBaseListener, this);
    return;
  }

  if (interfaceName == wl_output_interface.name) {
    bindOutput(registry, name, version);
    return;
  }

  if (interfaceName == wp_fractional_scale_manager_v1_interface.name) {
    m_fractionalScaleManager =
        static_cast<wp_fractional_scale_manager_v1 *>(wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1));
    return;
  }

  if (interfaceName == wp_viewporter_interface.name) {
    m_viewporter = static_cast<wp_viewporter *>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
  }
}
