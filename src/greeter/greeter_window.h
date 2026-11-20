#pragma once
#include "wayland/toplevel_surface.h"

class GlSharedContext;
class GreeterSurface;
class RenderContext;
class WaylandClient;

class GreeterWindow : public ToplevelSurface {
public:
  GreeterWindow(WaylandClient& client, GlSharedContext& gl, RenderContext& context, GreeterSurface& surface);
  bool createSurface();
  void bindOutput(wl_output* output);
  void setSceneReady(bool ready);
  void matchOutputLogicalSize();
  void requestLayout();
  void requestRedraw();
  void renderNow();

protected:
  void render() override;

private:
  WaylandClient& m_client;
  GreeterSurface& m_greeterSurface;
  wl_output* m_boundOutput = nullptr;
  bool m_sceneReady = false;
  bool m_forceRender = true;
  bool m_loggedFirstFrame = false;
};
