#include "greeter/greeter_window.h"

#include "core/log.h"
#include "fractional-scale-v1-client-protocol.h"
#include "greeter/greeter_surface.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/node.h"
#include "viewporter-client-protocol.h"
#include "wayland/wayland_client.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cmath>
#include <wayland-client.h>

namespace {
constexpr Logger kLog("greeter-window");

const xdg_surface_listener kXdgSurfaceListener = {
    .configure = &GreeterWindow::handleXdgSurfaceConfigure,
};

const xdg_toplevel_listener kToplevelListener = {
    .configure = &GreeterWindow::handleToplevelConfigure,
    .close = &GreeterWindow::handleToplevelClose,
    .configure_bounds = &GreeterWindow::handleToplevelConfigureBounds,
    .wm_capabilities = &GreeterWindow::handleToplevelWmCapabilities,
};

const wl_callback_listener kFrameListener = {
    .done = &GreeterWindow::handleFrameDone,
};

const wp_fractional_scale_v1_listener kFractionalScaleListener = {
    .preferred_scale = &GreeterWindow::handleFractionalPreferredScale,
};
} // namespace

GreeterWindow::GreeterWindow(WaylandClient &client, GlSharedContext &gl,
                             RenderContext &renderContext,
                             GreeterSurface &surface)
    : m_client(client), m_gl(gl), m_renderContext(renderContext),
      m_greeterSurface(surface) {}

GreeterWindow::~GreeterWindow() {
  destroyRoleObjects();
  destroySurface();
}

bool GreeterWindow::createSurface() {
  if (!m_client.hasXdgShell() || m_client.compositor() == nullptr) {
    kLog.error("missing xdg-shell or compositor");
    return false;
  }

  m_wlSurface = wl_compositor_create_surface(m_client.compositor());
  if (m_wlSurface == nullptr) {
    kLog.error("wl_compositor_create_surface failed");
    return false;
  }

  if (m_client.fractionalScaleManager() != nullptr) {
    m_fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(
        m_client.fractionalScaleManager(), m_wlSurface);
    wp_fractional_scale_v1_add_listener(m_fractionalScale,
                                        &kFractionalScaleListener, this);
  }
  if (m_client.viewporter() != nullptr) {
    m_viewport = wp_viewporter_get_viewport(m_client.viewporter(), m_wlSurface);
  }

  m_xdgSurface = xdg_wm_base_get_xdg_surface(m_client.xdgWmBase(), m_wlSurface);
  if (m_xdgSurface == nullptr) {
    destroySurface();
    return false;
  }
  xdg_surface_add_listener(m_xdgSurface, &kXdgSurfaceListener, this);

  m_toplevel = xdg_surface_get_toplevel(m_xdgSurface);
  if (m_toplevel == nullptr) {
    destroyRoleObjects();
    destroySurface();
    return false;
  }

  xdg_toplevel_add_listener(m_toplevel, &kToplevelListener, this);
  xdg_toplevel_set_title(m_toplevel, "Noctalia Greeter");
  xdg_toplevel_set_app_id(m_toplevel, "dev.noctalia.Greeter");

  // Initial commit (shell ToplevelSurface::initialize).
  wl_surface_commit(m_wlSurface);
  if (m_client.flush() < 0) {
    kLog.error("Wayland flush failed during window init");
    destroyRoleObjects();
    destroySurface();
    return false;
  }

  kLog.info("greeter surface created");
  return true;
}

void GreeterWindow::setSceneReady(bool ready) {
  m_sceneReady = ready;
  if (!ready) {
    return;
  }

  if (m_pendingConfigureSerial != 0) {
    const std::uint32_t serial = m_pendingConfigureSerial;
    const std::uint32_t width = m_pendingConfigureWidth;
    const std::uint32_t height = m_pendingConfigureHeight;
    m_pendingConfigureSerial = 0;
    acknowledgeConfigure(width, height, serial);
    return;
  }

  if (m_configured) {
    m_layoutNeeded = true;
    m_redrawNeeded = true;
    renderNow();
  }
}

void GreeterWindow::requestLayout() {
  m_layoutNeeded = true;
  m_redrawNeeded = true;
  renderNow();
}

void GreeterWindow::requestRedraw() {
  m_redrawNeeded = true;
  renderNow();
}

void GreeterWindow::bindOutput(wl_output *output) {
  m_boundOutput = output;
  applySurfaceScale();
}

void GreeterWindow::handleFractionalPreferredScale(
    void *data, struct wp_fractional_scale_v1 * /*fs*/, std::uint32_t scale) {
  auto *self = static_cast<GreeterWindow *>(data);
  self->m_surfaceScale = static_cast<float>(scale) / 120.0f;
  self->m_client.setOutputPreferredScale(self->m_boundOutput,
                                         self->m_surfaceScale);
  self->applySurfaceScale();

  if (!self->m_configured || self->m_width == 0 || self->m_height == 0) {
    return;
  }

  self->m_renderTarget.destroy();
  self->m_layoutNeeded = true;
  self->m_redrawNeeded = true;
  if (self->m_sceneReady) {
    self->renderNow();
  }
}

void GreeterWindow::matchOutputLogicalSize() {
  if (m_lastToplevelWidth > 0 && m_lastToplevelHeight > 0) {
    return;
  }

  const auto logical = m_client.logicalSizeForOutput(m_boundOutput);
  if (!logical) {
    return;
  }

  if (m_width != logical->first || m_height != logical->second ||
      !m_configured) {
    applyConfigure(logical->first, logical->second);
  }
}

void GreeterWindow::renderNow() {
  if (!m_sceneReady || m_width == 0 || m_height == 0 ||
      m_wlSurface == nullptr) {
    return;
  }

  paintFrame();
  requestNextFrame();
}

void GreeterWindow::applySurfaceScale() {
  if (m_wlSurface == nullptr) {
    return;
  }

  const bool useFractional = m_fractionalScale != nullptr &&
                             m_surfaceScale > 0.0f && m_viewport != nullptr;
  if (useFractional) {
    wl_surface_set_buffer_scale(m_wlSurface, 1);
    if (m_width > 0 && m_height > 0) {
      wp_viewport_set_destination(m_viewport, m_width, m_height);
    }
    m_bufferScale = 1;
    return;
  }

  m_bufferScale = m_client.outputBufferScale(m_boundOutput);
  wl_surface_set_buffer_scale(m_wlSurface, m_bufferScale);
}

float GreeterWindow::effectiveSurfaceScale() const noexcept {
  if (m_fractionalScale != nullptr && m_surfaceScale > 0.0f) {
    return m_surfaceScale;
  }
  return static_cast<float>(std::max(1, m_bufferScale));
}

std::uint32_t
GreeterWindow::bufferWidthForLogical(std::uint32_t logical) const noexcept {
  return std::max<std::uint32_t>(
      1, static_cast<std::uint32_t>(std::lround(static_cast<float>(logical) *
                                                effectiveSurfaceScale())));
}

std::uint32_t
GreeterWindow::bufferHeightForLogical(std::uint32_t logical) const noexcept {
  return std::max<std::uint32_t>(
      1, static_cast<std::uint32_t>(std::lround(static_cast<float>(logical) *
                                                effectiveSurfaceScale())));
}

bool GreeterWindow::ensureRenderTarget() {
  const std::uint32_t bufferWidth = bufferWidthForLogical(m_width);
  const std::uint32_t bufferHeight = bufferHeightForLogical(m_height);

  if (m_renderTarget.isReady()) {
    if (m_renderTarget.bufferWidth() != bufferWidth ||
        m_renderTarget.bufferHeight() != bufferHeight) {
      m_renderTarget.destroy();
    }
  }

  if (m_renderTarget.isReady()) {
    m_renderTarget.resize(bufferWidth, bufferHeight);
    m_renderTarget.setLogicalSize(m_width, m_height);
    m_renderContext.syncContentScale(m_renderTarget);
    return true;
  }

  m_renderTarget.create(m_wlSurface, m_renderContext);
  m_renderTarget.resize(bufferWidth, bufferHeight);
  m_renderTarget.setLogicalSize(m_width, m_height);
  m_renderContext.syncContentScale(m_renderTarget);

  if (!m_renderTarget.isReady()) {
    kLog.error("render target not ready ({}x{} logical, {}x{} buffer)", m_width,
               m_height, bufferWidth, bufferHeight);
    return false;
  }

  return true;
}

void GreeterWindow::paintFrame() {
  if (!ensureRenderTarget()) {
    return;
  }

  const bool needsLayout =
      m_layoutNeeded || m_greeterSurface.sceneRoot()->layoutDirty();
  const bool needsRedraw = m_redrawNeeded || needsLayout ||
                           m_greeterSurface.sceneRoot()->paintDirty();

  if (!needsRedraw) {
    return;
  }

  m_greeterSurface.prepareFrame(m_width, m_height, needsLayout);
  m_layoutNeeded = false;
  m_redrawNeeded = false;

  m_renderContext.renderScene(m_renderTarget, m_greeterSurface.sceneRoot());
  m_greeterSurface.sceneRoot()->clearDirty();

  if (!m_loggedFirstFrame) {
    kLog.info(
        "presented first frame {}x{} logical ({}x{} buffer, scale={:.2f})",
        m_width, m_height, m_renderTarget.bufferWidth(),
        m_renderTarget.bufferHeight(), effectiveSurfaceScale());
    m_loggedFirstFrame = true;
  }
}

void GreeterWindow::requestNextFrame() {
  if (m_wlSurface == nullptr || m_frameCallback != nullptr) {
    return;
  }

  m_frameCallback = wl_surface_frame(m_wlSurface);
  if (m_frameCallback == nullptr) {
    return;
  }

  wl_callback_add_listener(m_frameCallback, &kFrameListener, this);
  if (m_client.flush() < 0) {
    kLog.error("Wayland flush failed while requesting next frame");
  }
}

void GreeterWindow::handleFrameDone(void *data, wl_callback *callback,
                                    std::uint32_t /*time*/) {
  auto *self = static_cast<GreeterWindow *>(data);
  wl_callback_destroy(callback);
  self->m_frameCallback = nullptr;

  if (!self->m_sceneReady) {
    return;
  }

  const bool dirty = self->m_redrawNeeded || self->m_layoutNeeded ||
                     self->m_greeterSurface.sceneRoot()->paintDirty() ||
                     self->m_greeterSurface.sceneRoot()->layoutDirty();
  if (dirty) {
    self->paintFrame();
  }

  self->requestNextFrame();
}

void GreeterWindow::applyConfigure(std::uint32_t width, std::uint32_t height) {
  width = std::max<std::uint32_t>(1, width);
  height = std::max<std::uint32_t>(1, height);

  const int32_t nextBufferScale = m_client.outputBufferScale(m_boundOutput);
  const bool sizeChanged =
      m_width != width || m_height != height || !m_configured;
  const bool scaleChanged =
      m_fractionalScale == nullptr && m_bufferScale != nextBufferScale;

  if (sizeChanged || scaleChanged) {
    m_renderTarget.destroy();
    m_width = width;
    m_height = height;
    m_configured = true;
    m_layoutNeeded = true;
    m_redrawNeeded = true;
    applySurfaceScale();
    kLog.info("configured {}x{} logical (scale={:.2f})", m_width, m_height,
              effectiveSurfaceScale());
  } else {
    applySurfaceScale();
  }

  if (m_sceneReady) {
    renderNow();
  }
}

void GreeterWindow::acknowledgeConfigure(std::uint32_t width,
                                         std::uint32_t height,
                                         std::uint32_t serial) {
  if (m_xdgSurface == nullptr) {
    return;
  }

  xdg_surface_ack_configure(m_xdgSurface, serial);

  if (m_frameCallback != nullptr) {
    wl_callback_destroy(m_frameCallback);
    m_frameCallback = nullptr;
  }
  m_redrawNeeded = true;
  applyConfigure(width, height);
}

void GreeterWindow::handleXdgSurfaceConfigure(void *data, xdg_surface *surface,
                                              std::uint32_t serial) {
  auto *self = static_cast<GreeterWindow *>(data);

  std::uint32_t width = self->m_pendingWidth > 0
                            ? static_cast<std::uint32_t>(self->m_pendingWidth)
                            : 0;
  std::uint32_t height = self->m_pendingHeight > 0
                             ? static_cast<std::uint32_t>(self->m_pendingHeight)
                             : 0;
  if (self->m_lastToplevelWidth > 0) {
    width = static_cast<std::uint32_t>(self->m_lastToplevelWidth);
  }
  if (self->m_lastToplevelHeight > 0) {
    height = static_cast<std::uint32_t>(self->m_lastToplevelHeight);
  }
  if (width == 0 || height == 0) {
    const auto logical =
        self->m_client.logicalSizeForOutput(self->m_boundOutput);
    if (logical) {
      if (width == 0) {
        width = logical->first;
      }
      if (height == 0) {
        height = logical->second;
      }
    }
  }
  if (width == 0) {
    width = 1280;
  }
  if (height == 0) {
    height = 720;
  }

  if (!self->m_sceneReady) {
    self->m_pendingConfigureSerial = serial;
    self->m_pendingConfigureWidth = width;
    self->m_pendingConfigureHeight = height;
    return;
  }

  self->acknowledgeConfigure(width, height, serial);
  (void)surface;
}

void GreeterWindow::handleToplevelConfigure(void *data,
                                            xdg_toplevel * /*toplevel*/,
                                            std::int32_t width,
                                            std::int32_t height,
                                            wl_array * /*states*/) {
  auto *self = static_cast<GreeterWindow *>(data);
  if (width > 0) {
    self->m_lastToplevelWidth = width;
    self->m_pendingWidth = width;
  }
  if (height > 0) {
    self->m_lastToplevelHeight = height;
    self->m_pendingHeight = height;
  }
}

void GreeterWindow::handleToplevelClose(void * /*data*/,
                                        xdg_toplevel * /*toplevel*/) {
  kLog.info("toplevel close requested");
}

void GreeterWindow::handleToplevelConfigureBounds(void * /*data*/,
                                                  xdg_toplevel * /*toplevel*/,
                                                  std::int32_t /*width*/,
                                                  std::int32_t /*height*/) {}

void GreeterWindow::handleToplevelWmCapabilities(void * /*data*/,
                                                 xdg_toplevel * /*toplevel*/,
                                                 wl_array * /*capabilities*/) {}

void GreeterWindow::destroyRoleObjects() {
  if (m_frameCallback != nullptr) {
    wl_callback_destroy(m_frameCallback);
    m_frameCallback = nullptr;
  }
  if (m_fractionalScale != nullptr) {
    wp_fractional_scale_v1_destroy(m_fractionalScale);
    m_fractionalScale = nullptr;
  }
  if (m_viewport != nullptr) {
    wp_viewport_destroy(m_viewport);
    m_viewport = nullptr;
  }
  if (m_toplevel != nullptr) {
    xdg_toplevel_destroy(m_toplevel);
    m_toplevel = nullptr;
  }
  if (m_xdgSurface != nullptr) {
    xdg_surface_destroy(m_xdgSurface);
    m_xdgSurface = nullptr;
  }
  m_renderTarget.destroy();
}

void GreeterWindow::destroySurface() {
  if (m_wlSurface != nullptr) {
    wl_surface_destroy(m_wlSurface);
    m_wlSurface = nullptr;
  }
}
