#pragma once

#include <EGL/egl.h>

struct wl_display;

// Root surfaceless EGL context shared by all greeter render contexts.
class GlSharedContext {
public:
  GlSharedContext() = default;
  ~GlSharedContext();

  GlSharedContext(const GlSharedContext&) = delete;
  GlSharedContext& operator=(const GlSharedContext&) = delete;

  void initialize(wl_display* display);
  void cleanup();

  [[nodiscard]] EGLDisplay display() const noexcept { return m_display; }
  [[nodiscard]] EGLConfig config() const noexcept { return m_config; }
  [[nodiscard]] EGLContext rootContext() const noexcept { return m_rootContext; }

  // Bind the root context surfacelessly. Handy when a GL resource has to be
  // created before any rendering surface exists.
  void makeCurrentSurfaceless() const;

private:
  EGLDisplay m_display = EGL_NO_DISPLAY;
  EGLConfig m_config = nullptr;
  EGLContext m_rootContext = EGL_NO_CONTEXT;
};
