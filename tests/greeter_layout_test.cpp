#include "greeter/greeter_config_store.h"
#include "greeter/greeter_layout.h"
#include "greeter/greeter_model.h"
#include "greeter/greeter_preferences.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  void check(const greeter::GreeterLayout& layout, float w, float h) {
    require(layout.components[0].visible, "login disappeared");
    for (unsigned i = 0; i < 6; i++) {
      const auto& r = layout.components[i];
      if (!r.visible)
        continue;
      require(std::isfinite(r.x) && std::isfinite(r.y), "non-finite geometry");
      require(
          r.x >= 0 && r.y >= 0 && r.x + r.width <= w + 1 && r.y + r.height <= h + 1, "component escaped the output"
      );
      for (unsigned j = 0; j < i; j++)
        require(!greeter::rectanglesOverlap(r, layout.components[j]), "visible components overlap");
    }
  }
} // namespace
int main() {
  std::filesystem::path directory;
  try {
    greeter::GreeterLayoutConfig defaults;
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
    require(config.layout.loginPresentation == greeter::LoginPresentation::Full, "login mode was lost");
    require(
        !config.layout.components[0].visible
            && !config.layout.components[0].output
            && !config.layout.components[0].x
            && !config.layout.components[0].width,
        "invalid login settings survived validation"
    );
    require(config.layout.components[0].anchor == "center-left", "valid sibling setting lost");
    require(
        config.layout.components[1].visible == false
            && config.layout.components[1].y == -32
            && !config.layout.components[1].anchor,
        "partial component fallback failed"
    );
    require(greeter::config::configDiagnostics().size() == 6, "layout diagnostics missing");
    require(greeter::config::writeConfig(path, config), "round trip write failed");
    auto restored = greeter::config::loadConfig(path);
    require(
        restored.layout.components[1].visible == false
            && restored.layout.components[0].anchor == "center-left"
            && restored.layout.loginPresentation == greeter::LoginPresentation::Full,
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
            && greeter::config::loadConfig(path).layout.components[1].visible == false,
        "UI preference save rewrote administrator layout"
    );
    std::filesystem::create_directories(directory / "data/wayland-sessions");
    std::ofstream(directory / "data/wayland-sessions/sample.desktop")
        << "[Desktop Entry]\nName=Layout Test Session\nExec=/bin/true\nType=Application\n";
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
    std::ofstream(path) << "[user]\ndefault='layout-test-account'\n[session]\ndefault='Missing Session'\n";
    GreeterModel missing;
    missing.initialize();
    require(!missing.hasConfiguredDefaults(), "missing default session did not restore full login");
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
