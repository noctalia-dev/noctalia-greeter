#include "greeter/greeter_config_store.h"
#include "greeter/greeter_layout.h"
#include "greeter/greeter_model.h"
#include "greeter/greeter_preferences.h"
#include "greeter/ui/greeter_components.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  void check(const greeter::GreeterLayout& layout, float w, float h) {
    require(layout.components[0].visible, "login disappeared");
    for (unsigned i = 0; i < 6; i++) {
      const auto r = layout.bounds(static_cast<greeter::GreeterComponent>(i));
      if (!r.visible)
        continue;
      require(std::isfinite(r.x) && std::isfinite(r.y), "non-finite geometry");
      require(
          r.x >= 0 && r.y >= 0 && r.x + r.width <= w + 1 && r.y + r.height <= h + 1, "component escaped the output"
      );
      for (unsigned j = 0; j < i; j++)
        require(
            !greeter::rectanglesOverlap(r, layout.bounds(static_cast<greeter::GreeterComponent>(j))),
            "visible components overlap"
        );
    }
  }
  void checkTransforms() {
    using Id = greeter::GreeterComponent;
    for (const auto [w, h] :
         {std::pair{2560.0F, 1440.0F}, std::pair{1280.0F, 720.0F}, std::pair{320.0F, 520.0F},
          std::pair{720.0F, 1280.0F}}) {
      for (const auto angle : {0.0, 0.3, std::numbers::pi / 2, -std::numbers::pi}) {
        for (const auto scale : {0.5, 1.0, 4.0}) {
          for (const bool flipX : {false, true}) {
            for (const bool flipY : {false, true}) {
              greeter::GreeterLayoutConfig config;
              for (auto& part : config.components) {
                part.rotation = angle;
                part.scale = scale;
                part.flipX = flipX;
                part.flipY = flipY;
              }
              const auto layout = greeter::resolveGreeterLayout(config, w, h, 468, false, "DP-1", "monitor-id");
              check(layout, w, h);
              const auto& rect = layout[Id::Login];
              const auto& transform = layout.transforms[0];
              Node root;
              Node& reference = *root.addChild(std::make_unique<Node>());
              reference.setPosition(rect.x, rect.y);
              reference.setSize(rect.width, rect.height);
              reference.setRotation(transform.rotation);
              reference.setScale(transform.scale * (flipX ? -1.0F : 1.0F), transform.scale * (flipY ? -1.0F : 1.0F));
              float left, top, right, bottom;
              Node::transformedBounds(&reference, left, top, right, bottom);
              const auto bounds = layout.bounds(Id::Login);
              require(
                  std::abs(left - bounds.x) < 0.01F
                      && std::abs(top - bounds.y) < 0.01F
                      && std::abs(right - bounds.x - bounds.width) < 0.01F
                      && std::abs(bottom - bounds.y - bounds.height) < 0.01F,
                  "collision bounds differ from scene transform"
              );
              auto local = std::make_unique<Node>();
              local->setPosition(24, 90);
              local->setSize(rect.width - 48, 40);
              auto* expected = reference.addChild(std::move(local));
              // Match the small-view root scale used by the surface.
              root.setScale(0.5F);
              auto actual = std::make_unique<InputArea>();
              actual->setFocusable(true);
              actual->setPosition(rect.x + 24, rect.y + 90);
              actual->setSize(rect.width - 48, 40);
              auto* area = actual.get();
              greeter::applyComponentTransform(*area, rect, transform);
              root.addChild(std::move(actual));
              // Compare against a real parent/child transform at several control points.
              for (const auto [px, py] :
                   {std::pair{0.0F, 0.0F}, std::pair{area->width(), area->height()},
                    std::pair{area->width() / 2, area->height() / 2}}) {
                float ex, ey, ax, ay;
                Node::mapToScene(expected, px, py, ex, ey);
                Node::mapToScene(area, px, py, ax, ay);
                require(
                    std::abs(ex - ax) < 0.01F && std::abs(ey - ay) < 0.01F,
                    "component children do not share a transform center"
                );
              }
              InputDispatcher input;
              input.setSceneRoot(&root);
              float x, y;
              Node::mapToScene(area, area->width() / 2, area->height() / 2, x, y);
              require(input.inputAreaAt(x, y) == area, "transformed control missed pointer input");
              require(
                  input.cycleTabFocus(false) && input.focusedArea() == area, "transformed control lost keyboard focus"
              );
              greeter::applyComponentTransform(*area, rect, {});
              require(
                  area->scaleX() == 1
                      && area->scaleY() == 1
                      && area->rotation() == 0
                      && std::abs(area->translateX()) < 0.001F
                      && std::abs(area->translateY()) < 0.001F,
                  "removing transform retained scene state"
              );
            }
          }
        }
      }
    }
  }
} // namespace
int main() {
  std::filesystem::path directory;
  try {
    checkTransforms();
    greeter::GreeterLayoutConfig defaults;
    const greeter::LayoutRect bottomTrigger{1150, 650, 88, 36, true};
    auto popup = greeter::placeGreeterPopup(bottomTrigger, 280, 260, 1280, 720, true);
    require(
        popup.x + popup.width == bottomTrigger.x + bottomTrigger.width, "popup detached horizontally from its trigger"
    );
    require(popup.y + popup.height == bottomTrigger.y - 10, "footer popup did not open above trigger");
    const greeter::LayoutRect topTrigger{1150, 30, 88, 36, true};
    popup = greeter::placeGreeterPopup(topTrigger, 280, 260, 1280, 720, true);
    require(popup.y == topTrigger.y + topTrigger.height + 10, "popup did not flip below top trigger");
    popup = greeter::placeGreeterPopup({0, 0, 88, 36, true}, 5000, 5000, 320, 520, false);
    require(
        popup.x >= 12 && popup.y >= 12 && popup.x + popup.width <= 308 && popup.y + popup.height <= 508,
        "oversized popup escaped viewport padding"
    );
    for (auto [w, h] : {std::pair{1706.F, 960.F}, {1280.F, 720.F}, {720.F, 1280.F}, {640.F, 520.F}, {320.F, 520.F}}) {
      for (bool minimal : {false, true}) {
        auto layout = greeter::resolveGreeterLayout(defaults, w, h, minimal ? 270 : 468, minimal, "DP-1", "monitor-id");
        check(layout, w, h);
        if (w > 1000)
          require(layout.components[0].x > w / 2, "wide card lost its right anchor");
      }
    }
    auto settings = defaults;
    settings.components[0].visible = false;
    settings.components[0].output = "missing";
    settings.components[0].x = 8192;
    settings.components[0].y = -8192;
    settings.components[0].width = 1600;
    for (unsigned i = 1; i < 6; i++) {
      settings.components[i].anchor = "center";
      settings.components[i].width = 1600;
    }
    auto overlap = greeter::resolveGreeterLayout(settings, 1280, 720, 468, false, "DP-1", "monitor-id");
    check(overlap, 1280, 720);
    settings = defaults;
    settings.components[1].output = "missing";
    require(
        !greeter::resolveGreeterLayout(settings, 1706, 960, 468, false, "DP-1", "monitor-id").components[1].visible,
        "missing output matched clock"
    );
    settings.components[1].output = "monitor-id";
    require(
        greeter::resolveGreeterLayout(settings, 1706, 960, 468, false, "DP-1", "monitor-id").components[1].visible,
        "stable output identity did not match"
    );
    greeter::GreeterPreferences legacy;
    legacy.hideLogo = true;
    legacy.clockPosition = "top-right";
    legacy.powerButtonsPosition = "hidden";
    legacy.schemeSelectorPosition = "top-left";
    auto effective = greeter::effectiveGreeterLayout(legacy);
    require(
        effective.components[1].anchor == "top-right"
            && effective.components[2].visible == false
            && effective.components[3].visible == false
            && effective.components[4].anchor == "top-left",
        "legacy mapping changed"
    );
    legacy.layout.components[1].anchor = "bottom-left";
    legacy.layout.components[2].visible = true;
    legacy.layout.components[3].visible = true;
    effective = greeter::effectiveGreeterLayout(legacy);
    require(
        effective.components[1].anchor == "bottom-left"
            && effective.components[2].visible == true
            && effective.components[3].visible == true,
        "explicit fields lost to legacy settings"
    );
    char pattern[] = "/tmp/noctalia-layout-XXXXXX";
    const char* created = ::mkdtemp(pattern);
    require(created, "mkdtemp failed");
    directory = created;
    const auto path = directory / "greeter.toml";
    std::ofstream(path) << R"([layout]
login_mode = "full"
[layout.login]
visible = false
output = "missing"
anchor = "center-left"
x = 9000
width = nan
[layout.clock]
anchor = "invalid"
y = -32
visible = false
[layout.unknown]
visible = true
)";
    greeter::config::clearConfigDiagnostics();
    auto config = greeter::config::loadConfig(path);
    require(config.layout->loginPresentation == greeter::LoginPresentation::Full, "login mode was lost");
    require(
        !config.layout->components[0].visible
            && !config.layout->components[0].output
            && !config.layout->components[0].x
            && !config.layout->components[0].width,
        "invalid login settings survived validation"
    );
    require(config.layout->components[0].anchor == "center-left", "valid sibling setting lost");
    require(
        config.layout->components[1].visible == false
            && config.layout->components[1].y == -32
            && !config.layout->components[1].anchor,
        "partial component fallback failed"
    );
    require(greeter::config::configDiagnostics().size() == 6, "layout diagnostics missing");
    const auto invalidSize = std::filesystem::file_size(path);
    require(!greeter::config::writeConfig(path, config), "writer replaced invalid layout source");
    require(std::filesystem::file_size(path) == invalidSize, "refused write changed invalid file");
    std::filesystem::remove(path);
    require(!greeter::config::writeConfig(path, config), "writer accepted stale invalid layout");
    config.layoutValid = true; // Explicitly retain valid siblings in a new, repaired fixture.
    require(greeter::config::writeConfig(path, config), "round trip write failed");
    auto restored = greeter::config::loadConfig(path);
    require(
        restored.layout->components[1].visible == false
            && restored.layout->components[0].anchor == "center-left"
            && restored.layout->loginPresentation == greeter::LoginPresentation::Full,
        "layout round trip changed settings"
    );
    ::setenv("NOCTALIA_GREETER_STATE_DIR", directory.c_str(), 1);
    std::ofstream(directory / "sync.toml") << "[layout.clock]\nvisible=true\n[appearance]\nscheme='Noctalia'\n";
    auto prefs = greeter::loadGreeterPreferences();
    require(prefs.layout.components[1].visible == false, "mutable layout overrode administrator setting");
    const auto before = std::filesystem::file_size(path);
    require(greeter::saveGreeterPreferences(prefs), "preference save failed");
    require(
        std::filesystem::file_size(path) == before
            && greeter::config::loadConfig(path).layout->components[1].visible == false,
        "UI preference save rewrote administrator layout"
    );
    std::filesystem::create_directories(directory / "data/wayland-sessions");
    std::ofstream(directory / "data/wayland-sessions/sample.desktop")
        << "[Desktop Entry]\nName=Layout Test Session\nExec=/bin/true\nType=Application\n"
           "[Desktop Action Other]\nName=Other\nExec=/bin/false\nHidden=true\nNoDisplay=true\n";
    ::setenv("XDG_DATA_DIRS", (directory / "data").c_str(), 1);
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n";
    std::ofstream(directory / "sync.toml") << "[session]\nlast='Layout Test Session'\n";
    GreeterModel remembered;
    remembered.initialize();
    require(!remembered.hasConfiguredDefaults(), "remembered session incorrectly enabled minimal login");
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n[session]\ndefault='Layout Test Session'\n";
    GreeterModel configured;
    configured.initialize();
    require(configured.hasConfiguredDefaults(), "resolved defaults did not enable minimal login");
    require(
        configured.sessions()[configured.state().selectedSession].command == "/bin/true",
        "action group changed configured session command"
    );
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n[session]\ndefault='sample'\n";
    GreeterModel desktopId;
    desktopId.initialize();
    require(desktopId.hasConfiguredDefaults(), "desktop ID default did not enable minimal login");
    greeter::setCliDefaultSession("sample");
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n[session]\ndefault='Missing Session'\n";
    GreeterModel cli;
    cli.initialize();
    require(cli.hasConfiguredDefaults(), "CLI session default did not override missing configured session");
    greeter::setCliDefaultSession(std::nullopt);
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n[session]\ndefault='Missing Session'\n";
    GreeterModel missing;
    missing.initialize();
    require(!missing.hasConfiguredDefaults(), "missing default session did not restore full login");
    std::ofstream(path) << "[layout.login]\nwidth=500\n[layout.outputs.DP-2.login]\ny=12\n";
    std::ofstream(directory / "sync.toml") << "[layout.login]\nanchor='center'\nx=10\n[layout.outputs.DP-1.login]\nx="
                                              "99\nwidth=400\n[layout.outputs.DP-2]\nlogin_mode='minimal'\n";
    greeter::GreeterPreferences clockPrefs;
    greeter::WidgetInstance scopedClock;
    scopedClock.id = "greeter-default-clock-0123456789abcdef0123456789abcdef";
    scopedClock.type = "clock";
    scopedClock.output = "DP-1";
    clockPrefs.widgets = greeter::WidgetCollection{std::vector{scopedClock}};
    clockPrefs.defaultClockOverrides.emplace();
    clockPrefs.defaultClockOverrides->placement.visible = true;
    const auto localClocks = greeter::resolveGreeterWidgetProfile(clockPrefs, "DP-1");
    require(
        localClocks.instances.size() == 2 && localClocks.instances[0].id == greeter::kDefaultTimeWidget,
        "output clock lost its default identity or duplicated the administrator-required clock"
    );
    const auto scopedPrefs = greeter::loadGreeterPreferences();
    const auto firstOutput = greeter::effectiveGreeterLayout(scopedPrefs, "DP-1");
    const auto secondOutput = greeter::effectiveGreeterLayout(scopedPrefs, "DP-2");
    require(
        firstOutput.components[0].x == 99 && firstOutput.components[0].width == 500,
        "scoped Sync/admin precedence failed"
    );
    require(
        secondOutput.components[0].x == 10 && secondOutput.components[0].y == 12,
        "output layouts leaked between monitors"
    );
    require(secondOutput.loginPresentation == greeter::LoginPresentation::Minimal, "per-output login mode lost");
    const auto disconnected = greeter::effectiveGreeterLayout(scopedPrefs, "other");
    require(disconnected.components[0].x == 10, "unmatched output inherited another monitor's layout");
    auto scopedFile = greeter::config::loadConfig(path);
    require(greeter::config::writeConfig(path, scopedFile), "scoped layout round trip failed");
    require(
        greeter::config::loadConfig(path).layout->outputs.at("DP-2").components[0].y == 12,
        "scoped layout lost on write"
    );
    std::filesystem::remove_all(directory);
    std::puts("layout: bounds, overlap, output matching, invalid-field fallback, round trip and precedence passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "layout: %s\n", error.what());
    if (!directory.empty())
      std::filesystem::remove_all(directory);
    return 1;
  }
}
