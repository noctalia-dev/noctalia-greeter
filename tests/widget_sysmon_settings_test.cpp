#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  greeter::WidgetInstance sysmonWidget() {
    greeter::WidgetInstance widget;
    widget.id = "metrics";
    widget.type = "sysmon";
    return widget;
  }
  void accepts(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = sysmonWidget();
    widget.settings.emplace(key, std::move(value));
    require(greeter::validateWidgetSettings(widget).empty(), "valid Sysmon setting rejected");
  }
  void rejects(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = sysmonWidget();
    widget.settings.emplace(key, std::move(value));
    const auto path = "widgets.widget.metrics.settings." + std::string(key);
    const auto issues = greeter::validateWidgetSettings(widget);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing Sysmon setting diagnostic"
    );
  }
} // namespace

int main() {
  try {
    auto widget = sysmonWidget();
    const auto definitions = greeter::widgetSettingDefinitions(widget.type);
    require(definitions && definitions->size() == 16, "Sysmon schema is incomplete");
    std::set<std::string_view> keys;
    for (const auto& definition : *definitions) {
      require(keys.insert(definition.key).second, "duplicate Sysmon setting");
      widget.settings.emplace(definition.key, definition.defaultValue);
    }
    require(greeter::validateWidgetSettings(widget).empty(), "declared Sysmon defaults rejected");
    require(
        std::get<std::string>(widget.settings.at("stat")) == "cpu_usage"
            && std::get<std::string>(widget.settings.at("stat2")).empty()
            && std::get<std::string>(widget.settings.at("display")) == "graph"
            && std::get<std::string>(widget.settings.at("gauge_layout")) == "horizontal"
            && std::get<std::string>(widget.settings.at("color")) == "primary"
            && std::get<std::string>(widget.settings.at("color2")) == "secondary"
            && std::get<std::string>(widget.settings.at("highlight_color")) == "error"
            && std::get<std::int64_t>(widget.settings.at("label_min_width")) == 0,
        "Sysmon defaults diverged from the Noctalia contract"
    );
    widget.settings.clear();
    require(
        greeter::validateWidgetSettings(widget).empty() && widget.settings.empty(),
        "omitted Sysmon settings rejected or materialized"
    );
    for (const auto key : std::array<std::string_view, 2>{"stat", "stat2"}) {
      for (const auto stat :
           std::array<std::string_view, 5>{"cpu_usage", "cpu_temp", "cpu_freq", "ram_pct", "swap_pct"})
        accepts(key, std::string(stat));
      for (const auto stat : std::array<std::string_view, 11>{
               "gpu_temp", "gpu_usage", "gpu_vram", "gpu_vram_used", "net_rx", "net_tx", "cpu", "CPU_USAGE",
               " cpu_usage", "cpu_usage ", "other"
           })
        rejects(key, std::string(stat));
    }
    accepts("stat2", std::string{});
    rejects("stat", std::string{});
    accepts("display", std::string("graph"));
    accepts("display", std::string("gauge"));
    accepts("gauge_layout", std::string("horizontal"));
    accepts("gauge_layout", std::string("vertical"));
    rejects("display", std::string("Graph"));
    rejects("gauge_layout", std::string("vertical "));
    for (const auto key : std::array<std::string_view, 4>{"stat", "stat2", "display", "gauge_layout"}) {
      rejects(key, std::string("unknown"));
      for (const auto& value : std::vector<greeter::WidgetSettingValue>{
               true, std::int64_t{0}, 0.0, std::vector<std::string>{"cpu_usage"},
               greeter::WidgetStringMap{{"stat", "cpu_usage"}}
           })
        rejects(key, value);
    }
    accepts("label_min_width", std::int64_t{0});
    accepts("label_min_width", std::int64_t{200});
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             std::int64_t{-1}, std::int64_t{201}, 12.0, true, std::string("12"),
             std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()
         })
      rejects("label_min_width", value);
    for (const auto key : std::array<std::string_view, 3>{"color", "color2", "highlight_color"}) {
      accepts(key, std::string("on_surface"));
      accepts(key, std::string("#aabbcc80"));
      rejects(key, std::string{});
      rejects(key, std::string("invalid-color"));
      rejects(key, false);
    }
    for (const auto key : std::array<std::string_view, 2>{"show_label", "shadow"}) {
      accepts(key, true);
      accepts(key, false);
      rejects(key, std::int64_t{1});
      rejects(key, std::string("true"));
    }
    accepts("font_family", std::string{});
    accepts("font_family", std::string("An Uninstalled Font"));
    rejects("font_family", false);
    accepts("background", false);
    accepts("background_color", std::string("surface_container"));
    accepts("background_opacity", 0.333);
    accepts("background_radius", std::int64_t{32});
    accepts("background_padding", std::int64_t{0});
    rejects("background_opacity", 1.1);
    rejects("background_radius", 12.0);
    rejects("background_padding", std::int64_t{-1});

    for (const auto* metric : {"gpu_temp", "gpu_usage", "gpu_vram", "gpu_vram_used"}) {
      for (const auto* display : {"graph", "gauge"}) {
        widget.settings = {
            {"display", std::string(display)},
            {"stat", std::string("cpu_usage")},
            {"stat2", std::string(metric)},
            {"show_label", false}
        };
        require(!greeter::validateWidgetSettings(widget).empty(), "hidden unsupported GPU metric admitted");
      }
    }
    widget.settings = {{"display", std::string("gauge")}, {"stat2", std::string("net_rx")}};
    require(!greeter::validateWidgetSettings(widget).empty(), "hidden secondary network stat admitted");
    widget.settings = {{"display", std::string("graph")}, {"show_label", false}, {"label_min_width", 12.0}};
    require(!greeter::validateWidgetSettings(widget).empty(), "hidden label width bypassed validation");
    for (const auto key : std::array<std::string_view, 9>{
             "interface", "network_speed_unit", "network_speed_compact", "network_speed_decimal_places",
             "network_speed_label_style", "network_future_option", "command", "action", "title"
         })
      rejects(key, std::string("unsupported"));
    widget = sysmonWidget();
    widget.type = "label";
    widget.settings = {{"stat", std::string("cpu_usage")}};
    require(!greeter::validateWidgetSettings(widget).empty(), "Sysmon metric leaked into Label schema");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-sysmon-settings-test: {}", error.what());
    return 1;
  }
}
