#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/library_runtime.h"
#include "greeter/resources.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/glyph_node.h"
#include "render/scene/input_area.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/context_menu.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scrollbar.h"
#include "wayland/wayland_client.h"

#include <EGL/egl.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
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

  class ConfigurableGreeterWindow : public GreeterWindow {
  public:
    using GreeterWindow::GreeterWindow;
    using Surface::onConfigure;
  };

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
      drainGreeterUiWork();
      require(wl_display_dispatch_pending(display) >= 0, "dispatch_pending failed");
      drainGreeterUiWork();
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
      const int uiTimeout = greeterUiPollTimeoutMs();
      const int timeoutMs = uiTimeout < 0 ? static_cast<int>(remaining.count())
                                          : std::min(static_cast<int>(remaining.count()), uiTimeout);
      const int result = poll(&fd, 1, timeoutMs);
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

  void testAccountSwitchLayout(GreeterWindow& window, GreeterSurface& surface, GreeterModel& model) {
    window.requestLayout();
    Input* password = nullptr;
    Button* submit = nullptr;
    Box* panel = nullptr;
    for (const auto& child : surface.sceneRoot()->children()) {
      if (!child->visible())
        continue;
      if (auto* input = dynamic_cast<Input*>(child.get()))
        password = input;
      if (auto* button = dynamic_cast<Button*>(child.get()); button && button->label()->text() == "Sign in")
        submit = button;
      if (auto* box = dynamic_cast<Box*>(child.get()); box && (!panel || box->height() > panel->height()))
        panel = box;
    }
    require(password && submit && panel, "account switch controls missing");
    const auto geometry = [&] {
      return std::array<float, 12>{panel->x(),    panel->y(),    panel->width(),    panel->height(),
                                   password->x(), password->y(), password->width(), password->height(),
                                   submit->x(),   submit->y(),   submit->width(),   submit->height()};
    };
    const auto original = geometry();
    const auto username = model.state().username;
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    require(!model.state().passwordVisible, "escape did not open account selection");
    require(geometry() == original, "card moved or resized when entering account selection");
    require(
        password->visible() && !password->enabled(), "account selection did not reserve a disabled credential field"
    );
    surface.onKeyEvent(XKB_KEY_Down, 0, 0, true, false);
    require(geometry() == original, "opening account search moved the card");
    surface.onKeyEvent(XKB_KEY_Down, 0, 0, true, false);
    surface.onKeyEvent(XKB_KEY_Return, 0, 0, true, false);
    require(
        model.state().passwordVisible && model.state().username != username, "search did not switch to another account"
    );
    require(geometry() == original, "selecting another account moved or resized the card");
    require(
        password->enabled() && password->value().empty(), "switched account did not restore a clean credential field"
    );
    const auto selected = model.state().username;
    password->setValue("dismiss-test");
    InputArea* account = nullptr;
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* area = dynamic_cast<InputArea*>(child.get()); area
          && area->visible()
          && area->width() == password->width()
          && area->height() > 40
          && area->y() < password->y())
        account = area;
    require(account, "account dropdown anchor missing");
    const auto open = [&] {
      const float x = account->x() + 8, y = account->y() + 8;
      surface.onPointerMotion(x, y);
      surface.onPointerEvent(x, y, BTN_LEFT, true);
      surface.onPointerEvent(x, y, BTN_LEFT, false);
      require(
          model.state().passwordVisible && model.state().username == selected,
          "opening dropdown changed the selected account"
      );
    };
    open();
    surface.onPointerMotion(12, 12);
    surface.onPointerEvent(12, 12, BTN_LEFT, true);
    surface.onPointerEvent(12, 12, BTN_LEFT, false);
    require(
        model.state().passwordVisible && model.state().username == selected && password->value() == "dismiss-test",
        "outside dismissal reset the selected login"
    );
    require(password->inputArea()->focused(), "outside dismissal did not restore credential focus");
    const auto repeatedBlankClicks = [&](InputArea* expected) {
      for (unsigned click = 0; click < 5; click++) {
        // Match Greeter::setupInputCallbacks: every pointer event updates the active view.
        surface.setKeyboardOwner(true);
        surface.onPointerMotion(12, 12);
        require(expected->focused(), "background motion changed keyboard focus");
        surface.setKeyboardOwner(true);
        surface.onPointerEvent(12, 12, BTN_LEFT, true);
        require(expected->focused(), "background press cleared keyboard focus");
        surface.setKeyboardOwner(true);
        surface.onPointerEvent(12, 12, BTN_LEFT, false);
        require(expected->focused(), "background release changed keyboard focus");
      }
    };
    repeatedBlankClicks(password->inputArea());
    open();
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    require(
        model.state().passwordVisible
            && model.state().username == selected
            && password->value() == "dismiss-test"
            && password->inputArea()->focused(),
        "escape dismissal reset the selected login or focus"
    );
    // Also cover dismissal before a password step, where the account row owns focus.
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    require(!model.state().passwordVisible && account->focused(), "account picker did not receive keyboard focus");
    const float ax = account->x() + 8, ay = account->y() + 8;
    surface.onPointerMotion(ax, ay);
    surface.onPointerEvent(ax, ay, BTN_LEFT, true);
    surface.onPointerEvent(ax, ay, BTN_LEFT, false);
    surface.onPointerMotion(12, 12);
    surface.onPointerEvent(12, 12, BTN_LEFT, true);
    surface.onPointerEvent(12, 12, BTN_LEFT, false);
    require(account->focused(), "picker dismissal did not restore account-row focus");
    repeatedBlankClicks(account);
    surface.onKeyEvent(XKB_KEY_Return, 0, 0, true, false);
    require(
        model.state().passwordVisible && password->inputArea()->focused(),
        "account selection did not restore credential focus"
    );
    password->setValue("");
    std::puts("layout: account selection/search/switch preserve card and credential positions");
  }

  void testPowerButtonClicks(WaylandClient& client, GreeterWindow& window, GreeterSurface& surface) {
    window.requestLayout();
    Input* password = nullptr;
    Button* power = nullptr;
    for (const auto& child : surface.sceneRoot()->children()) {
      if (auto* input = dynamic_cast<Input*>(child.get()); input && input->visible())
        password = input;
      if (auto* button = dynamic_cast<Button*>(child.get());
          button && button->visible() && button->label()->text() == "Power")
        power = button;
    }
    require(password && power, "password and power entry were not visible");
    surface.onPointerEvent(password->x() + 8, password->y() + 8, BTN_LEFT, true);
    surface.onPointerEvent(password->x() + 8, password->y() + 8, BTN_LEFT, false);
    dispatchFor(client, 100ms);
    const float x = power->x() + power->width() / 2, y = power->y() + power->height() / 2;
    surface.onPointerMotion(x, y);
    dispatchFor(client, 100ms);
    const auto before = swaps;
    surface.onPointerEvent(x, y, BTN_LEFT, true);
    require(dispatchUntil(client, 5s, [&] { return swaps > before; }), "password blur did not paint before release");
    surface.onPointerEvent(x, y, BTN_LEFT, false);
    dispatchFor(client, 100ms);
    Button* restart = nullptr;
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* b = dynamic_cast<Button*>(child.get()); b && b->visible() && b->label()->text() == "Restart")
        restart = b;
    require(restart, "first power click after password focus did not open the menu");
    surface.onKeyEvent(XKB_KEY_a, 'a', 0, true, false);
    require(password->value().empty(), "power menu leaked typing into the credential field");
    const float rx = restart->x() + restart->width() / 2, ry = restart->y() + restart->height() / 2;
    surface.onPointerMotion(rx, ry);
    surface.onPointerEvent(rx, ry, BTN_LEFT, true);
    surface.onPointerEvent(rx, ry, BTN_LEFT, false);
    dispatchFor(client, 100ms);
    const char* powerLog = std::getenv("NOCTALIA_GREETER_TEST_POWER_LOG");
    require(powerLog, "safe power-command fixture missing");
    std::ifstream marker(powerLog);
    std::string command;
    std::getline(marker, command);
    require(command == "reboot", "power selection did not execute its command directly");
    std::getline(marker, command);
    require(command.empty(), "power selection executed more than once");
    require(!restart->visible(), "power popup did not close after action selection");
    require(password->inputArea()->focused(), "power action did not restore credential focus");
    surface.onPointerMotion(x, y);
    surface.onPointerEvent(x, y, BTN_LEFT, true);
    surface.onPointerEvent(x, y, BTN_LEFT, false);
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    // The same menu must be reachable without a pointer, with a deliberate return target.
    require(power->inputArea()->focused(), "closing power menu did not restore entry focus");
    surface.onKeyEvent(XKB_KEY_Return, 0, 0, true, false);
    require(restart->visible(), "keyboard activation did not reopen power menu");
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* b = dynamic_cast<Button*>(child.get()); b && b->label()->text() == "Cancel")
        require(!b->visible(), "main power menu still contains Cancel");
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    // Pointer dismissal returns to credentials and must not leave the footer active.
    surface.onKeyEvent(XKB_KEY_Return, 0, 0, true, false);
    surface.onPointerEvent(10, 10, BTN_LEFT, true);
    surface.onPointerEvent(10, 10, BTN_LEFT, false);
    require(!restart->visible(), "outside click did not dismiss power menu");
    require(!power->inputArea()->focused() && !power->hovered(), "outside click left Power highlighted");
    require(password->inputArea()->focused(), "outside click did not restore credential focus");
    std::puts("pointer: power opens on first click, executes once directly and clears highlight on dismissal");
  }

  void testPowerMenuResize(WaylandClient& client, ConfigurableGreeterWindow& window, GreeterSurface& surface) {
    // Open with the pointer after the prior test returned to the password field.
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* button = dynamic_cast<Button*>(child.get());
          button && button->visible() && button->label()->text() == "Power") {
        const float x = button->x() + button->width() / 2, y = button->y() + button->height() / 2;
        surface.onPointerEvent(x, y, BTN_LEFT, true);
        surface.onPointerEvent(x, y, BTN_LEFT, false);
      }
    const auto check = [&](float width, float height) {
      Box* panel = nullptr;
      for (const auto& child : surface.sceneRoot()->children())
        if (auto* box = dynamic_cast<Box*>(child.get()); box && box->visible() && box->zIndex() == 100)
          panel = box;
      require(panel, "power menu disappeared during resize");
      float l, t, r, b;
      Node::transformedBounds(surface.sceneRoot(), l, t, r, b);
      require(
          std::abs(l) < 1 && std::abs(t) < 1 && std::abs(r - width) < 1 && std::abs(b - height) < 1,
          "configure resized the buffer without relaying out the scene"
      );
      Node::transformedBounds(panel, l, t, r, b);
      require(l >= 0 && t >= 0 && r <= width + 1 && b <= height + 1, "resized power menu escaped the window");
      if (width >= 1000 && height >= 600) {
        for (const auto& child : surface.sceneRoot()->children()) {
          auto* trigger = dynamic_cast<Button*>(child.get());
          if (!trigger || !trigger->visible() || trigger->label()->text() != "Power")
            continue;
          float tl, tt, tr, tb;
          Node::transformedBounds(trigger, tl, tt, tr, tb);
          require(std::abs(r - tr) < 1 && std::abs(b - (tt - 10)) < 1, "power popup is not attached above its trigger");
        }
      }
      for (const auto& child : surface.sceneRoot()->children()) {
        auto* button = dynamic_cast<Button*>(child.get());
        if (!button || !button->visible() || button->zIndex() != 102)
          continue;
        float bl, bt, br, bb;
        Node::transformedBounds(button, bl, bt, br, bb);
        require(bl >= l && bt >= t && br <= r + 1 && bb <= b + 1, "resized power action escaped its panel");
      }
      return panel->height();
    };
    for (auto [width, height] : {std::pair{640U, 360U}, {320U, 480U}, {344U, 285U}, {1280U, 720U}}) {
      const auto before = swaps;
      // Exercise the production configure path; no manual requestLayout can mask a missing callback.
      window.onConfigure(width, height);
      require(dispatchUntil(client, 5s, [&] { return swaps > before; }), "resize did not paint");
      check(static_cast<float>(width), static_cast<float>(height));
    }
    check(1280, 720);
    Button* restart = nullptr;
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* button = dynamic_cast<Button*>(child.get());
          button && button->visible() && button->label()->text() == "Restart")
        restart = button;
    require(restart, "restart entry missing after resize");
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    surface.onKeyEvent(XKB_KEY_Return, 0, 0, true, false);
    surface.onKeyEvent(XKB_KEY_Down, 0, 0, true, false);
    require(restart->inputArea()->focused(), "power menu keyboard navigation did not focus restart");
    surface.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    std::puts("resize: power menu follows window configure bounds");
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
    dispatcher.pointerButton(10.0f, 10.0f, BTN_LEFT, true, 0, 0, false);

    require(area->pressed() && area->hovered(), "unrelated cleanup cleared pointer state");
    auto detached = root.removeChild(subtree);
    dispatcher.pointerMotion(10.0f, 10.0f, 0);
    require(!area->pressed() && !area->hovered(), "removed subtree retained hover or capture");
    require(leaves == 1, "removed hovered area did not receive exactly one leave event");
    detached.reset();
    dispatcher.pointerMotion(10.0f, 10.0f, 0);
    dispatcher.pointerButton(10.0f, 10.0f, BTN_LEFT, false, 0, 0, false);
    require(clicks == 0, "release activated a removed menu row");
  }

  void testMenuScrollbar(WaylandClient& client, GreeterSurface& surface, GreeterModel& model) {
    surface.onKeyEvent(XKB_KEY_F7, 0, 0, true, false);
    dispatchFor(client, 100ms);
    Box* panel = nullptr;
    Button* anchor = nullptr;
    for (const auto& child : surface.sceneRoot()->children()) {
      if (auto* box = dynamic_cast<Box*>(child.get()); box && box->visible() && box->zIndex() == 60)
        panel = box;
      if (auto* button = dynamic_cast<Button*>(child.get());
          button && button->visible() && button->label()->text() == "Appearance")
        anchor = button;
    }
    require(panel && anchor, "appearance popup or trigger missing");
    require(
        std::abs(panel->x() + panel->width() - anchor->x() - anchor->width()) < 1
            && std::abs(panel->y() + panel->height() - (anchor->y() - 10)) < 1,
        "appearance popup is not attached above its trigger"
    );
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

    // Activate a shared menu row with the pointer. Selection must finish after
    // dispatch, safely close the row's owning control, and return input focus.
    surface.onKeyEvent(XKB_KEY_F7, 0, 0, true, false);
    dispatchFor(client, 100ms);
    ContextMenuControl* menu = nullptr;
    for (const auto& child : surface.sceneRoot()->children()) {
      auto* popup = dynamic_cast<Box*>(child.get());
      if (!popup || !popup->visible() || popup->zIndex() != 60)
        continue;
      for (const auto& content : popup->children())
        if (auto* control = dynamic_cast<ContextMenuControl*>(content.get()))
          menu = control;
    }
    require(menu && menu->entryCount() > 1, "shared appearance menu missing rows");
    const auto current = menu->highlightedIndex(), next = (current + 1) % menu->entryCount();
    const auto expected = model.state().selectedScheme - current + next;
    float ml, mt, mr, mb;
    Node::transformedBounds(menu, ml, mt, mr, mb);
    const float rowX = (ml + mr) / 2;
    const float rowY = mt + (menu->rowTop(next) + menu->rowBottom(next)) / 2;
    surface.onPointerMotion(rowX, rowY);
    surface.onPointerEvent(rowX, rowY, BTN_LEFT, true);
    surface.onPointerEvent(rowX, rowY, BTN_LEFT, false);
    dispatchFor(client, 100ms);
    require(model.state().selectedScheme == expected, "pointer activation selected the wrong appearance");
    for (const auto& child : surface.sceneRoot()->children()) {
      if (auto* popup = dynamic_cast<Box*>(child.get()); popup && popup->zIndex() == 60)
        require(!popup->visible(), "appearance menu did not close after pointer activation");
      if (auto* input = dynamic_cast<Input*>(child.get()); input && input->visible())
        require(input->inputArea()->focused(), "appearance selection did not return credential focus");
    }
    std::puts("pointer: removed rows release capture and menu scrollbar dragging survives relayout");
  }

  void testFrameLoop() {
    initializeGreeterLibrary();
    WaylandClient client;
    require(client.connect(), "could not connect to the headless compositor");
    require(
        dispatchUntil(client, 5s, [&]() { return !client.readyOutputsSorted().empty(); }),
        "headless output was not announced"
    );
    GlSharedContext gl;
    gl.initialize(client.display());
    initializeGreeterAssets();
    RenderContext context;
    context.initialize(gl);
    greeter::setCliDefaultUser("pointer-test");
    GreeterModel model;
    model.initialize();
    GreetdClient greetd;
    GreeterAuthController auth(model, greetd);
    GreeterSurface surface(model, auth, 1);
    model.setOnChanged([&](GreeterViewId source, bool selection) { surface.syncModel(selection && source != 1); });
    surface.initialize(&context);
    surface.setKeyboardOwner(true);
    ConfigurableGreeterWindow window(client, gl, context, surface);
    surface.setWindow(&window);
    const auto outputs = client.readyOutputsSorted();
    window.bindOutput(outputs.front()->output);
    require(window.createSurface(), "could not create the test window");

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
    const auto beforeBlink = swaps;
    require(
        dispatchUntil(client, 5s, [&] { return swaps > beforeBlink; }), "focused input blink timer did not repaint"
    );
    // A focused shared Input intentionally repaints its blinking cursor. Check
    // idle scheduling with focus removed, so the scene has no periodic work.
    surface.setKeyboardOwner(false);
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
    surface.setKeyboardOwner(true);
    for (auto [width, height] : {std::pair{320U, 480U}, {640U, 360U}, {720U, 1280U}}) {
      surface.prepareFrame(width, height, true);
      for (const auto& child : surface.sceneRoot()->children()) {
        const bool credential = dynamic_cast<Input*>(child.get()) != nullptr;
        auto* button = dynamic_cast<Button*>(child.get());
        if (!child->visible() || (!credential && !(button && button->label()->text() == "Sign in")))
          continue;
        float left, top, right, bottom;
        Node::transformedBounds(child.get(), left, top, right, bottom);
        require(
            left >= 0 && top >= 0 && right <= static_cast<float>(width) + 1 && bottom <= static_cast<float>(height) + 1,
            "responsive view clipped a credential control"
        );
      }
    }
    window.requestLayout();
    testAccountSwitchLayout(window, surface, model);
    testPowerButtonClicks(client, window, surface);
    testPowerMenuResize(client, window, surface);
    testMenuScrollbar(client, surface, model);
    if (!model.sessions().empty()) {
      greeter::setCliDefaultSession(model.sessions().front().name);
      GreeterModel compactModel;
      compactModel.initialize();
      require(compactModel.hasConfiguredDefaults(), "test defaults did not resolve");
      GreetdClient compactGreetd;
      GreeterAuthController compactAuth(compactModel, compactGreetd);
      GreeterSurface compact(compactModel, compactAuth, 2);
      compact.initialize(&context);
      compact.setWindow(&window);
      compact.prepareFrame(1280, 720, true);
      unsigned visibleInputs = 0;
      for (const auto& child : compact.sceneRoot()->children()) {
        if (!child->visible())
          continue;
        if (dynamic_cast<Input*>(child.get()))
          ++visibleInputs;
        if (auto* label = dynamic_cast<Label*>(child.get()))
          require(label->text() != "Session", "configured-default card retained session picker");
      }
      require(visibleInputs == 1, "compact card lost its credential field");
      require(compactAuth.showUserPicker(2), "compact card could not return to account selection");
      require(!compactModel.state().passwordVisible, "compact account escape did not leave the password step");
      greeter::setCliDefaultSession(std::nullopt);
    }
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
