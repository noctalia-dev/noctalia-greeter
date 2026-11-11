#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/button.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/scrollbar.h"
#include "wayland/wayland_client.h"

#include <EGL/egl.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdexcept>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {

  using namespace std::chrono_literals;

  std::size_t swaps = 0;
  bool frameCallbackPending = false;
  bool swappedWithoutCallback = false;

  // Observe the real window callback without replacing its frame handling.
  const wl_callback_listener kFrameListener = {
      .done = [](void* data, wl_callback* callback, std::uint32_t time) {
        frameCallbackPending = false;
        GreeterWindow::handleFrameDone(data, callback, time);
      },
  };

  void require(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  bool dispatchUntil(WaylandClient& client, std::chrono::milliseconds timeout, const std::function<bool()>& done) {
    wl_display* display = client.display();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      require(wl_display_dispatch_pending(display) >= 0, "dispatch_pending failed");
      require(!swappedWithoutCallback, "a frame callback was requested after the EGL swap/commit");
      if (done()) {
        return true;
      }
      const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining <= 0ms) {
        return false;
      }
      if (wl_display_prepare_read(display) != 0) {
        continue;
      }
      if (client.flush() < 0) {
        wl_display_cancel_read(display);
        throw std::runtime_error("Wayland flush failed");
      }
      pollfd fd{.fd = wl_display_get_fd(display), .events = POLLIN, .revents = 0};
      const int result = poll(&fd, 1, static_cast<int>(remaining.count()));
      if (result > 0 && (fd.revents & POLLIN) != 0) {
        require(wl_display_read_events(display) >= 0, "Wayland read failed");
      } else {
        wl_display_cancel_read(display);
        require(result >= 0 || errno == EINTR, "Wayland poll failed");
        require((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0, "Wayland connection closed");
      }
    }
  }

  void dispatchFor(WaylandClient& client, std::chrono::milliseconds duration) {
    (void)dispatchUntil(client, duration, []() { return false; });
  }

  void requireIdle(WaylandClient& client, GreeterWindow& window) {
    // Drain the callback for the last painted frame before checking for idle.
    dispatchFor(client, 100ms);
    const auto before = swaps;
    window.renderNow();
    dispatchFor(client, 100ms);
    require(swaps == before, "an idle window kept swapping buffers");
  }

  void testPowerButtonClicks(WaylandClient& client, GreeterWindow& window, GreeterSurface& surface) {
    Input* password = nullptr;
    std::vector<Button*> powerButtons;
    for (const auto& child : surface.sceneRoot()->children()) {
      if (auto* input = dynamic_cast<Input*>(child.get()); input != nullptr && input->visible()) {
        password = input;
      }
      auto* button = dynamic_cast<Button*>(child.get());
      if (button == nullptr || button->glyph() == nullptr || !button->visible()) {
        continue;
      }
      const auto glyph = button->glyph()->codepoint();
      if (glyph == GlyphRegistry::lookup("power")
          || glyph == GlyphRegistry::lookup("reload")
          || glyph == GlyphRegistry::lookup("cpu")) {
        powerButtons.push_back(button);
      }
    }
    require(password != nullptr, "password input was not visible for the pointer regression");
    require(powerButtons.size() >= 2, "shutdown and restart buttons were not found");

    unsigned clicks = 0;
    // Replace every power callback before dispatching any pointer input.
    for (Button* button : powerButtons) {
      button->setOnClick([&]() { ++clicks; });
    }
    for (Button* button : powerButtons) {
      clicks = 0;
      password->inputArea()->focus();
      window.requestLayout();
      dispatchFor(client, 100ms);
      const float x = button->x() + button->width() * 0.5f;
      const float y = button->y() + button->height() * 0.5f;
      surface.onPointerMotion(x, y);
      dispatchFor(client, 100ms);
      const auto before = swaps;
      surface.onPointerEvent(x, y, BTN_LEFT, true);
      require(
          dispatchUntil(client, 5s, [&]() { return swaps > before; }),
          "password blur did not paint a frame between press and release"
      );
      surface.onPointerEvent(x, y, BTN_LEFT, false);
      require(clicks == 1, "first power-button click was lost after password blur relayout");
    }
    for (Button* button : powerButtons) {
      button->setOnClick({});
    }
    std::puts("pointer: power buttons activate on the first click after password focus");
  }

  void testPointerRemoval() {
    Node root;
    root.setSize(100.0f, 100.0f);
    auto menu = std::make_unique<Node>();
    menu->setSize(50.0f, 50.0f);
    auto row = std::make_unique<InputArea>();
    row->setSize(50.0f, 50.0f);
    unsigned leaves = 0;
    unsigned clicks = 0;
    row->setOnLeave([&]() { ++leaves; });
    row->setOnClick([&](const InputArea::PointerData&) { ++clicks; });
    auto* area = static_cast<InputArea*>(menu->addChild(std::move(row)));
    Node* subtree = root.addChild(std::move(menu));
    InputDispatcher dispatcher;
    dispatcher.setSceneRoot(&root);
    dispatcher.pointerEnter(10.0f, 10.0f, 0);
    dispatcher.pointerButton(10.0f, 10.0f, BTN_LEFT, true);

    Node unrelated;
    dispatcher.invalidatePointersFor(&unrelated);
    dispatcher.invalidatePointersFor(nullptr);
    require(area->pressed() && area->hovered(), "unrelated cleanup cleared pointer state");
    dispatcher.invalidatePointersFor(subtree);
    require(!area->pressed() && !area->hovered(), "removed subtree retained hover or capture");
    require(leaves == 1, "removed hovered area did not receive exactly one leave event");
    (void)root.removeChild(subtree);
    dispatcher.pointerMotion(10.0f, 10.0f, 0);
    dispatcher.pointerButton(10.0f, 10.0f, BTN_LEFT, false);
    require(clicks == 0, "release activated a removed menu row");
  }

  void testMenuScrollbar(WaylandClient& client, GreeterSurface& surface) {
    surface.onKeyEvent(XKB_KEY_F7, 0, 0, true, false);
    dispatchFor(client, 100ms);
    Scrollbar* scrollbar = nullptr;
    for (const auto& child : surface.sceneRoot()->children()) {
      if (auto* bar = dynamic_cast<Scrollbar*>(child.get()); bar != nullptr && bar->visible()) {
        scrollbar = bar;
      }
    }
    require(scrollbar != nullptr, "scheme menu did not have a scrollbar");
    InputArea* thumb = nullptr;
    for (const auto& child : scrollbar->children()) {
      if (auto* area = dynamic_cast<InputArea*>(child.get()); area != nullptr && area->height() < scrollbar->height()) {
        thumb = area;
      }
    }
    require(thumb != nullptr, "scheme scrollbar thumb was not found");
    float left, top, right, bottom;
    Node::transformedBounds(thumb, left, top, right, bottom);
    const float x = (left + right) * 0.5f;
    const float y = (top + bottom) * 0.5f;
    surface.onPointerMotion(x, y);
    dispatchFor(client, 100ms);
    surface.onPointerEvent(x, y, BTN_LEFT, true);
    surface.onPointerMotion(x, y + 40.0f);
    dispatchFor(client, 100ms);
    require(thumb->pressed(), "menu row rebuild dropped scrollbar capture");
    float movedLeft, movedTop, movedRight, movedBottom;
    Node::transformedBounds(thumb, movedLeft, movedTop, movedRight, movedBottom);
    require(movedTop > top, "dragging the scheme scrollbar did not scroll the menu");
    surface.onPointerMotion(x, y + 60.0f);
    dispatchFor(client, 100ms);
    require(thumb->pressed(), "continued scrollbar drag lost capture");

    // Close the menu while its thumb is captured, then dispatch more input.
    // The scrollbar and its input areas have been destroyed at this point.
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    surface.onPointerMotion(x, y);
    surface.onPointerEvent(x, y, BTN_LEFT, false);
    std::puts("pointer: removed rows release capture and menu scrollbar dragging survives relayout");
  }

  void testFrameLoop() {
    WaylandClient client;
    require(client.connect(), "could not connect to the headless compositor");
    require(
        dispatchUntil(client, 5s, [&]() { return !client.readyOutputsSorted().empty(); }),
        "headless output was not announced"
    );
    GlSharedContext gl;
    gl.initialize(client.display());
    RenderContext context;
    context.initialize(gl);
    GlyphRegistry::initialize();
    greeter::setCliDefaultUser("pointer-test");
    GreeterSurface surface;
    surface.initialize(&context);
    surface.setKeyboardOwner(true);
    GreeterWindow window(client, gl, context, surface);
    surface.setWindow(&window);
    require(window.createSurface(), "could not create the test window");
    const auto outputs = client.readyOutputsSorted();
    window.bindOutput(outputs.front()->output);

    auto* animations = surface.sceneRoot()->animationManager();
    require(animations != nullptr, "scene has no animation manager");
    int owner = 0;
    unsigned updates = 0;
    // Deliberately leave the scene clean in onUpdate: active animations alone
    // must keep prepareFrame() and the callback/commit cycle running.
    animations->animate(0.0f, 1.0f, 60000.0f, Easing::Linear, [&](float) { ++updates; }, nullptr, &owner);
    window.setSceneReady(true);
    require(
        dispatchUntil(client, 5s, [&]() { return updates >= 4; }),
        "animation stalled without input or an unrelated commit"
    );
    animations->cancelForOwner(&owner);
    require(!surface.animating(), "cancelled animation is still active");
    requireIdle(client, window);

    auto before = swaps;
    window.requestRedraw();
    require(swaps == before + 1, "explicit redraw did not paint");
    requireIdle(client, window);

    before = swaps;
    window.requestLayout();
    require(swaps == before + 1, "explicit layout did not paint");
    requireIdle(client, window);

    before = swaps;
    surface.sceneRoot()->markPaintDirty();
    window.renderNow();
    require(swaps == before + 1, "dirty scene paint did not paint");
    requireIdle(client, window);

    before = swaps;
    surface.sceneRoot()->markLayoutDirty();
    window.renderNow();
    require(swaps == before + 1, "dirty scene layout did not paint");
    requireIdle(client, window);

    // Wake from idle, finish naturally, and stop committing again.
    window.requestRedraw();
    updates = 0;
    animations->animate(0.0f, 1.0f, 200.0f, Easing::Linear, [&](float) { ++updates; }, nullptr, &owner);
    window.requestRedraw();
    require(dispatchUntil(client, 5s, [&]() { return !surface.animating(); }), "restarted animation did not finish");
    require(updates >= 2, "restarted animation did not advance over multiple frames");
    requireIdle(client, window);
    testPointerRemoval();
    testPowerButtonClicks(client, window, surface);
    testMenuScrollbar(client, surface);
    std::puts("frame loop: startup, animation, redraw/layout, idle, and restart passed");
  }

} // namespace

// Linker wrappers observe production requests and swaps, forwarding both to
// libwayland/EGL. In particular, these catch ordering regressions in either
// renderNow() or handleFrameDone(), even if a later configure masks the stall.
extern "C" int __real_wl_proxy_add_listener(wl_proxy* proxy, void (**implementation)(void), void* data);
extern "C" int __wrap_wl_proxy_add_listener(wl_proxy* proxy, void (**implementation)(void), void* data) {
  if (std::strcmp(wl_proxy_get_class(proxy), "wl_callback") == 0) {
    const auto* listener = reinterpret_cast<const wl_callback_listener*>(implementation);
    if (listener->done == &GreeterWindow::handleFrameDone) {
      frameCallbackPending = true;
      return wl_callback_add_listener(reinterpret_cast<wl_callback*>(proxy), &kFrameListener, data);
    }
  }
  return __real_wl_proxy_add_listener(proxy, implementation, data);
}

extern "C" EGLBoolean __real_eglSwapBuffers(EGLDisplay display, EGLSurface surface);
extern "C" EGLBoolean __wrap_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
  swappedWithoutCallback = swappedWithoutCallback || !frameCallbackPending;
  ++swaps;
  return __real_eglSwapBuffers(display, surface);
}

int main() {
  try {
    testFrameLoop();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "frame loop: %s\n", error.what());
    return 1;
  }
}
