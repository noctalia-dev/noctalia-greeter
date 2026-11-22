#include "greeter/greeter.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/library_runtime.h"
#include "greeter/resources.h"
#include "greeter/widgets/widget_layer.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "wayland/wayland_client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
  using namespace std::chrono_literals;
  std::vector<std::chrono::steady_clock::time_point> samples;
  std::set<greeter::CpuRamProvider*> providers;

  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }

  void dispatchFor(WaylandClient& client, std::chrono::milliseconds duration) {
    auto* display = client.display();
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
      drainGreeterUiWork();
      require(wl_display_dispatch_pending(display) >= 0, "dispatch failed");
      if (wl_display_prepare_read(display) != 0)
        continue;
      require(client.flush() >= 0, "flush failed");
      pollfd fd{.fd = wl_display_get_fd(display), .events = POLLIN, .revents = 0};
      const auto result = poll(&fd, 1, 10);
      if (result > 0 && (fd.revents & POLLIN) != 0)
        require(wl_display_read_events(display) >= 0, "read failed");
      else {
        wl_display_cancel_read(display);
        require(result >= 0, "poll failed");
      }
    }
  }

  greeter::WidgetInstance widget(std::string id, std::string type) {
    greeter::WidgetInstance result;
    result.id = std::move(id);
    result.type = std::move(type);
    result.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::TopLeft, 0, 80};
    result.boxWidth = 200;
    result.boxHeight = 64;
    return result;
  }

  greeter::config::GreeterConfigFile profile() {
    greeter::config::GreeterConfigFile config;
    config.userDefault = "widget-runtime-test";
    config.sessionDefault = "Widget Runtime Test";
    config.clockEnabled = false;
    config.appearanceHideLogo = true;
    std::vector<greeter::WidgetInstance> widgets;
    auto item = widget("caption", "label");
    item.settings["title"] = std::string("Runtime caption");
    widgets.push_back(item);
    item = widget("tick", "clock");
    item.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::TopCenter, 0, 0};
    item.settings["format"] = std::string("%S");
    widgets.push_back(item);
    item = widget("dual", "sysmon");
    item.settings["stat2"] = std::string("ram_pct");
    widgets.push_back(item);
    item = widget("gauge", "sysmon");
    item.settings = {
        {"stat", std::string("swap_pct")}, {"stat2", std::string("cpu_temp")}, {"display", std::string("gauge")}
    };
    widgets.push_back(item);
    item = widget("frequency", "sysmon");
    item.settings["stat"] = std::string("cpu_freq");
    item.output = "edid:widget-test";
    widgets.push_back(item);
    item = widget("offscreen", "sysmon");
    item.placement = greeter::WidgetRelativePlacement{-20000, 100, 1280, 720};
    item.settings["stat"] = std::string("cpu_temp");
    widgets.push_back(item);
    item = widget("action", "button");
    item.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::BottomLeft, 0, 0};
    item.transform = {-0.2, true, false};
    item.settings = {{"label", std::string("Widget Power")}, {"action", std::string("power")}};
    widgets.push_back(item);
    item = widget("compact-sessions", "button");
    item.settings = {{"label", std::string("Sessions")}, {"action", std::string("sessions")}};
    widgets.push_back(item);
    item = widget("disabled", "sysmon");
    item.enabled = false;
    item.settings["stat"] = std::string("cpu_temp");
    widgets.push_back(item);
    item = widget("disconnected", "sysmon");
    item.output = "disconnected-output";
    item.settings["stat"] = std::string("cpu_temp");
    widgets.push_back(item);
    config.widgets = greeter::WidgetCollection{widgets};
    return config;
  }

  greeter::WidgetLayer* layer(GreeterSurface& surface) {
    for (const auto& node : surface.sceneRoot()->children())
      if (auto* result = dynamic_cast<greeter::WidgetLayer*>(node.get()))
        return result;
    return nullptr;
  }

  std::vector<Label*> labels(Node& root) {
    std::vector<Label*> result;
    const auto visit = [&](const auto& self, Node& node) -> void {
      if (auto* label = dynamic_cast<Label*>(&node))
        result.push_back(label);
      for (const auto& child : node.children())
        self(self, *child);
    };
    visit(visit, root);
    return result;
  }

  bool hasText(Node& root, std::string_view text) {
    return std::ranges::any_of(labels(root), [text](const auto* label) { return label->text() == text; });
  }

  void click(GreeterSurface& surface, const Node& node) {
    float x, y;
    Node::mapToScene(&node, node.width() / 2, node.height() / 2, x, y);
    surface.onPointerMotion(x, y);
    surface.onPointerEvent(x, y, BTN_LEFT, true);
    surface.onPointerEvent(x, y, BTN_LEFT, false);
  }

  void testSurfaces() {
    require(greeter::config::writeConfig(greeter::greeterConfPath(), profile()), "fixture write failed");
    WaylandClient client;
    require(client.connect(), "connect failed");
    dispatchFor(client, 100ms);
    const auto outputs = client.readyOutputsSorted();
    require(outputs.size() == 2, "two headless outputs required");
    GlSharedContext gl;
    gl.initialize(client.display());
    initializeGreeterAssets();
    RenderContext context;
    context.initialize(gl);
    GreeterModel model;
    model.initialize();
    GreetdClient greetd;
    GreeterAuthController auth(model, greetd);
    GreeterSurface first(model, auth, 1);
    first.setBoundOutputName("HEADLESS-1");
    first.setOutputIdentifier("edid:widget-test");
    first.initialize(&context);
    first.setKeyboardOwner(true);
    GreeterWindow window(client, gl, context, first);
    first.setWindow(&window);
    window.bindOutput(outputs[0]->output);
    require(window.createSurface(), "window creation failed");
    first.prepareFrame(1280, 720, true);
    require(layer(first) && first.widgetMetricDemand() == greeter::CpuRamDemand{}, "unmapped view requested metrics");
    window.setSceneReady(true);
    dispatchFor(client, 100ms);
    require(layer(first)->children().size() == 8, "disabled or unmatched widget mounted");
    require(
        first.widgetMetricDemand() == greeter::CpuRamDemand{true, true, true, true, false},
        "visible metric demand incorrect"
    );
    require(!layer(first)->children()[5]->visible(), "offscreen Sysmon stayed visible");
    require(
        model.hasConfiguredDefaults() && !layer(first)->children()[7]->hitTestVisible(),
        "compact card exposed a Sessions widget action"
    );
    require(hasText(*layer(first), "Runtime caption"), "saved Label did not mount");
    for (const auto& child : first.sceneRoot()->children())
      if (auto* input = dynamic_cast<Input*>(child.get()))
        require(input->zIndex() > layer(first)->zIndex(), "widgets can cover login controls");

    GreeterSurface second(model, auth, 2);
    second.setBoundOutputName("HEADLESS-2");
    second.initialize(&context);
    GreeterWindow other(client, gl, context, second);
    second.setWindow(&other);
    other.bindOutput(outputs[1]->output);
    require(other.createSurface(), "second window creation failed");
    other.setSceneReady(true);
    dispatchFor(client, 100ms);
    require(
        layer(second)->children().size() == 7 && !second.widgetMetricDemand().frequency, "output identity was ignored"
    );
    const auto snapshot = greeter::SysmonSnapshot::create(
        std::array{
            greeter::SysmonMetricReading{greeter::SysmonMetric::CpuUsage, {0, {0}, greeter::SysmonGaugeReading{0}}},
            greeter::SysmonMetricReading{
                greeter::SysmonMetric::RamPct, {42, {0.42F}, greeter::SysmonGaugeReading{0.42F}}
            },
            greeter::SysmonMetricReading{greeter::SysmonMetric::CpuFreq, {2300, {}, greeter::SysmonGaugeReading{0.5F}}},
            greeter::SysmonMetricReading{
                greeter::SysmonMetric::SwapPct, {{}, {}, {}, greeter::SysmonAbsence::NotConfigured}
            },
        }
    );
    require(snapshot.has_value(), "snapshot fixture invalid");
    first.setWidgetMetrics(*snapshot);
    second.setWidgetMetrics(*snapshot);
    dispatchFor(client, 100ms);
    for (auto* scene : {&first, &second})
      require(
          hasText(*layer(*scene), "0%") && hasText(*layer(*scene), "42%") && hasText(*layer(*scene), "No swap"),
          "shared snapshot or no-swap state lost"
      );
    require(hasText(*layer(first), "2.3 GHz"), "frequency snapshot did not reach pinned widget");
    first.setWidgetMetrics(*greeter::SysmonSnapshot::create({}));
    dispatchFor(client, 100ms);
    require(hasText(*layer(first), "--") && !hasText(*layer(first), "42%"), "unavailable readings retained stale data");

    auto* action = layer(first)->children()[6].get();
    InputArea* trigger = nullptr;
    const auto find = [&](const auto& self, Node& node) -> void {
      if (auto* area = dynamic_cast<InputArea*>(&node); area && area->focusable() && area->tabStop())
        trigger = area;
      for (const auto& child : node.children())
        self(self, *child);
    };
    find(find, *action);
    require(trigger != nullptr, "native widget action not bound");
    click(first, *trigger);
    dispatchFor(client, 100ms);
    const auto menuOpen = [&] {
      return std::ranges::any_of(first.sceneRoot()->children(), [](const auto& node) {
        return node->zIndex() == 100 && node->visible() && dynamic_cast<Box*>(node.get());
      });
    };
    require(menuOpen(), "transformed widget did not open native Power menu");
    first.onKeyEvent(XKB_KEY_Escape, 0, 0, true, false);
    require(trigger->focused(), "widget Escape lost trigger focus");
    click(first, *trigger);
    first.setOutputIdentifier("different-identity");
    dispatchFor(client, 100ms);
    require(!menuOpen() && !first.widgetMetricDemand().frequency, "replacement retained menu or stale demand");

    auto* clock = layer(first)->children()[1].get();
    const auto before = labels(*clock).front()->text();
    dispatchFor(client, 1100ms);
    require(labels(*clock).front()->text() != before, "mounted Clock did not tick");
    window.setSceneReady(false);
    other.setSceneReady(false);
    require(
        first.widgetMetricDemand() == greeter::CpuRamDemand{} && second.widgetMetricDemand() == greeter::CpuRamDemand{},
        "inactive views retained demand"
    );
    const auto stopped = labels(*clock).front()->text();
    dispatchFor(client, 1100ms);
    require(labels(*clock).front()->text() == stopped, "inactive Clock kept ticking");
    window.setSceneReady(true);
    require(
        first.widgetMetricDemand().cpu && labels(*clock).front()->text() != stopped,
        "reactivation did not refresh Clock/demand"
    );
    window.setSceneReady(false);
    for (unsigned state = 0; state < 4; ++state) {
      auto config = profile();
      if (state == 0)
        config.widgets.reset();
      else if (state == 1)
        config.widgets = greeter::WidgetCollection{};
      else if (state == 2)
        config.widgets = greeter::WidgetCollection{std::vector<greeter::WidgetInstance>{}};
      else {
        auto sticker = widget("missing-image", "sticker");
        sticker.settings["image_path"] = std::string("widget-assets/unavailable.png");
        config.widgets = greeter::WidgetCollection{std::vector{sticker}};
      }
      require(greeter::config::writeConfig(greeter::greeterConfPath(), config), "state fixture write failed");
      GreeterSurface probe(model, auth, 3);
      probe.initialize(&context);
      // Reuse the live renderer for a scene-only probe; the kiosk admits one
      // native toplevel per output, and both outputs already have a view.
      probe.setWindow(&window);
      probe.prepareFrame(1280, 720, true);
      require(probe.widgetMetricDemand() == greeter::CpuRamDemand{}, "empty or rejected profile requested metrics");
      if (state == 2)
        require(layer(probe) && layer(probe)->children().empty(), "empty collection inserted default widgets");
      else
        require(!layer(probe), "default or rejected profile mounted a layer");
      if (state == 3) {
        require(
            std::ranges::any_of(
                labels(*probe.sceneRoot()),
                [](const auto* label) { return label->visible() && label->text().contains("Widget profile:"); }
            ),
            "unavailable image error was hidden"
        );
        require(hasText(*probe.sceneRoot(), "Sign in"), "profile error removed login");
      }
    }
    std::puts(
        "widget surfaces: placement, output matching, metrics, actions, replacement, ticking and inactivity passed"
    );
  }

  void testApplication(bool visible) {
    auto config = profile();
    if (!visible) {
      auto hidden = widget("hidden", "sysmon");
      hidden.output = "disconnected-output";
      config.widgets->instances = std::vector{hidden};
    }
    require(greeter::config::writeConfig(greeter::greeterConfPath(), config), "app fixture write failed");
    samples.clear();
    providers.clear();
    WaylandClient client;
    require(client.connect(), "application connect failed");
    Greeter greeter;
    require(greeter.initialize(client), "application initialize failed");
    std::atomic<bool> shutdown{false};
    Timer stop;
    stop.start(visible ? 2200ms : 200ms, [&] { shutdown = true; });
    require(greeter.run(client, shutdown) == 0, "application loop failed");
    if (visible) {
      require(
          samples.size() >= 2 && samples.size() <= 3 && providers.size() == 1,
          "metrics sampled per view or loop iteration"
      );
      for (std::size_t i = 1; i < samples.size(); ++i)
        require(samples[i] - samples[i - 1] >= 900ms, "metrics polling exceeded cadence");
    } else
      require(samples.empty(), "unmatched widgets started metric polling");
  }
} // namespace

const greeter::SysmonSnapshot&
realWidgetSample(greeter::CpuRamProvider* provider) asm("__real__ZN7greeter14CpuRamProvider6sampleEv");
const greeter::SysmonSnapshot&
wrappedWidgetSample(greeter::CpuRamProvider* provider) asm("__wrap__ZN7greeter14CpuRamProvider6sampleEv");
const greeter::SysmonSnapshot& wrappedWidgetSample(greeter::CpuRamProvider* provider) {
  samples.push_back(std::chrono::steady_clock::now());
  providers.insert(provider);
  return realWidgetSample(provider);
}

int main() {
  try {
    initializeGreeterLibrary();
    testSurfaces();
    testApplication(true);
    testApplication(false);
    std::puts("widget runtime: mounted profiles and shared demand-driven sampling passed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget runtime: {}", error.what());
    return 1;
  }
}
