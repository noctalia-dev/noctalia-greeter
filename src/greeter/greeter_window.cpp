#include "greeter/greeter_window.h"

#include "core/log.h"
#include "greeter/greeter_surface.h"
#include "wayland/wayland_client.h"

namespace {
  constexpr Logger kLog("greeter-window");
}

GreeterWindow::GreeterWindow(WaylandClient& client, GlSharedContext&, RenderContext& context, GreeterSurface& surface)
    : ToplevelSurface(client.connection()), m_client(client), m_greeterSurface(surface) {
  setRenderContext(&context);
  setSceneRoot(surface.sceneRoot());
  setAnimationManager(surface.sceneRoot()->animationManager());
  setHoldFirstFrameForPendingContent(false);
  holdFirstFrameUntil([this] { return m_sceneReady; });
  setPrepareFrameCallback([this](bool, bool needsLayout) {
    if (m_sceneReady)
      m_greeterSurface.prepareFrame(width(), height(), needsLayout);
  });
  setConfigureCallback([this](std::uint32_t, std::uint32_t) {
    // A resize must reflow the scene as well as resize its render target.
    // Queue the layout until configure handling is complete.
    m_forceRender = true;
    Surface::requestLayout();
  });
  setScaleChangedCallback([this](float scale) { m_client.setOutputPreferredScale(m_boundOutput, scale); });
}

bool GreeterWindow::createSurface() {
  const auto logical = m_client.logicalSizeForOutput(m_boundOutput);
  return initialize(
      m_boundOutput,
      {.width = logical ? logical->first : 1280,
       .height = logical ? logical->second : 720,
       .title = "Noctalia Greeter",
       .appId = "dev.noctalia.Greeter"}
  );
}
void GreeterWindow::bindOutput(wl_output* output) { m_boundOutput = output; }
void GreeterWindow::setSceneReady(bool ready) {
  m_sceneReady = ready;
  m_greeterSurface.setWidgetsActive(ready);
  if (ready)
    requestLayout();
}
void GreeterWindow::matchOutputLogicalSize() {
  // The role's configure dimensions are authoritative; output binding only seeds the first size.
  if (m_sceneReady)
    requestLayout();
}
void GreeterWindow::requestLayout() {
  m_forceRender = true;
  Surface::requestLayout();
  renderNow();
}
void GreeterWindow::requestRedraw() {
  m_forceRender = true;
  Surface::requestRedraw();
  renderNow();
}
void GreeterWindow::renderNow() {
  if (m_sceneReady
      && (m_forceRender
          || m_greeterSurface.sceneRoot()->layoutDirty()
          || m_greeterSurface.sceneRoot()->paintDirty()
          || m_greeterSurface.animating()))
    Surface::renderNow();
}
void GreeterWindow::render() {
  if (!m_sceneReady)
    return;
  Surface::render();
  m_forceRender = false;
  if (!m_loggedFirstFrame) {
    kLog.info(
        "presented first frame {}x{} logical ({}x{} buffer, scale={:.2f})", width(), height(),
        renderTarget().bufferWidth(), renderTarget().bufferHeight(), effectiveBufferScale()
    );
    m_loggedFirstFrame = true;
  }
}
