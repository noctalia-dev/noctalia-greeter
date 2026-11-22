#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
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
  greeter::WidgetInstance clockWidget() {
    greeter::WidgetInstance widget;
    widget.id = "clock1";
    widget.type = "clock";
    return widget;
  }
  void accepts(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = clockWidget();
    widget.settings.emplace(key, std::move(value));
    require(greeter::validateWidgetSettings(widget).empty(), "valid Clock setting rejected");
  }
  void rejects(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = clockWidget();
    widget.settings.emplace(key, std::move(value));
    const auto path = "widgets.widget.clock1.settings." + std::string(key);
    const auto issues = greeter::validateWidgetSettings(widget);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing Clock setting diagnostic"
    );
  }
} // namespace

int main() {
  try {
    auto widget = clockWidget();
    const auto definitions = greeter::widgetSettingDefinitions("clock");
    require(definitions && definitions->size() == 17, "Clock schema is incomplete");
    std::set<std::string_view> keys;
    for (const auto& definition : *definitions) {
      require(keys.insert(definition.key).second, "duplicate Clock setting");
      widget.settings.emplace(definition.key, definition.defaultValue);
    }
    require(greeter::validateWidgetSettings(widget).empty(), "declared Clock defaults rejected");
    require(
        std::get<std::string>(widget.settings.at("clock_style")) == "digital"
            && std::get<std::string>(widget.settings.at("format")) == "{:%H:%M}"
            && std::get<std::string>(widget.settings.at("timezone")).empty()
            && std::get<bool>(widget.settings.at("show_seconds")),
        "Clock defaults diverged from the Noctalia contract"
    );
    widget.settings.clear();
    require(
        greeter::validateWidgetSettings(widget).empty() && widget.settings.empty(),
        "omitted Clock settings rejected or materialized"
    );

    accepts("clock_style", std::string("digital"));
    accepts("clock_style", std::string("analog"));
    for (const auto style : std::array<std::string_view, 6>{"", "Digital", "Analog", " analog", "analog ", "other"})
      rejects("clock_style", std::string(style));
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             false, std::int64_t{0}, 0.0, std::vector<std::string>{"digital"},
             greeter::WidgetStringMap{{"style", "analog"}}
         })
      rejects("clock_style", value);

    for (const auto format :
         std::array<std::string_view, 5>{"", "%H:%M", "{:%H:%M:%S}", "literal text", "{{{:%H:%M}}}"})
      accepts("format", std::string(format));
    rejects("format", false);
    rejects("format", std::int64_t{12});
    rejects("format", std::vector<std::string>{"%H:%M"});
    for (const auto zone : std::array<std::string_view, 5>{"", "UTC", "Etc/UTC", "Europe/Berlin", "Pacific/Kiritimati"})
      accepts("timezone", std::string(zone));
    for (const auto zone : std::array<std::string_view, 8>{
             "Not/A_Timezone", "Europe/Berln", "Europe/berlin", " UTC", "UTC ", ":UTC", "/etc/localtime", "../UTC"
         })
      rejects("timezone", std::string(zone));
    rejects("timezone", false);
    rejects("timezone", std::int64_t{0});
    rejects("timezone", std::vector<std::string>{"UTC"});

    for (const auto key :
         std::array<std::string_view, 5>{"tight_text", "center_text", "shadow", "circle", "show_seconds"}) {
      accepts(key, true);
      accepts(key, false);
      rejects(key, std::int64_t{1});
      rejects(key, std::string("true"));
    }
    accepts("font_size", 0.0);
    accepts("font_size", 112.0);
    accepts("font_size", std::int64_t{19});
    rejects("font_size", -1.0);
    rejects("font_size", 4097.0);
    accepts("font_weight", std::int64_t{300});
    accepts("font_weight", std::int64_t{500});
    rejects("font_weight", std::int64_t{99});
    rejects("font_weight", 500.0);
    accepts("color", std::string("primary"));
    accepts("color", std::string("#abcdef80"));
    rejects("color", std::string{});
    rejects("color", std::string("invalid-color"));
    accepts("font_family", std::string{});
    accepts("font_family", std::string("An Uninstalled Font"));
    rejects("font_family", false);
    accepts("background", false);
    accepts("background_color", std::string("surface_container"));
    accepts("background_opacity", 0.5);
    accepts("background_radius", std::int64_t{32});
    accepts("background_padding", std::int64_t{0});
    rejects("background_opacity", 1.1);
    rejects("background_radius", 12.0);
    rejects("background_padding", std::int64_t{-1});

    widget.settings = {{"clock_style", std::string("analog")}, {"format", false}};
    require(!greeter::validateWidgetSettings(widget).empty(), "analog-only visibility bypassed format type checks");
    widget.settings = {{"clock_style", std::string("digital")}, {"circle", std::int64_t{1}}};
    require(!greeter::validateWidgetSettings(widget).empty(), "digital-only visibility bypassed circle type checks");
    for (const auto key : std::array<std::string_view, 9>{
             "style", "time_format", "date_format", "font_size", "date_font_size", "scale", "title", "action", "command"
         })
      rejects(key, std::string("unsupported"));
    auto label = clockWidget();
    label.type = "label";
    label.settings["clock_style"] = std::string("analog");
    require(!greeter::validateWidgetSettings(label).empty(), "Clock enum leaked into the Label schema");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-clock-settings-test: {}", error.what());
    return 1;
  }
}
