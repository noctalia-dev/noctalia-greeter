#include "greeter/widgets/widget_catalog.h"
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
  greeter::WidgetInstance buttonWidget() {
    greeter::WidgetInstance widget;
    widget.id = "control";
    widget.type = "button";
    return widget;
  }
  void accepts(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = buttonWidget();
    widget.settings.emplace(key, std::move(value));
    require(greeter::validateWidgetSettings(widget).empty(), "valid Button setting rejected");
  }
  void rejects(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = buttonWidget();
    widget.settings.emplace(key, std::move(value));
    const auto path = "widgets.widget.control.settings." + std::string(key);
    const auto issues = greeter::validateWidgetSettings(widget);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing Button setting diagnostic"
    );
  }
} // namespace

int main() {
  try {
    auto widget = buttonWidget();
    const auto definitions = greeter::widgetSettingDefinitions(widget.type);
    require(definitions && definitions->size() == 8, "Button schema is incomplete or inherited common fields");
    std::set<std::string_view> keys;
    for (const auto& definition : *definitions) {
      require(keys.insert(definition.key).second, "duplicate Button setting");
      widget.settings.emplace(definition.key, definition.defaultValue);
    }
    require(greeter::validateWidgetSettings(widget).empty(), "declared Button defaults rejected");
    require(
        std::get<bool>(widget.settings.at("background"))
            && std::get<std::string>(widget.settings.at("glyph")) == "heart"
            && std::get<std::string>(widget.settings.at("label")).empty()
            && std::get<std::string>(widget.settings.at("variant")) == "default"
            && std::get<std::string>(widget.settings.at("color")).empty()
            && std::get<std::string>(widget.settings.at("hover_background")) == "hover"
            && std::get<std::string>(widget.settings.at("font_family")).empty()
            && std::get<std::string>(widget.settings.at("action")) == "none",
        "Button defaults diverged from the Noctalia greeter contract"
    );
    widget.settings.clear();
    require(
        greeter::validateWidgetSettings(widget).empty() && widget.settings.empty(),
        "omitted Button settings rejected or materialized"
    );

    const auto action = std::ranges::find(*definitions, "action", &greeter::WidgetSettingDefinition::key);
    require(action != definitions->end(), "Button action metadata missing");
    std::set<std::string_view> actionNames;
    for (const auto name : action->enumValues) {
      require(actionNames.insert(name).second, "duplicate action metadata");
      require(greeter::parseWidgetAction(name).has_value(), "schema advertises an unsupported action");
      accepts("action", std::string(name));
    }
    require(actionNames.size() == greeter::widgetActionDefinitions().size(), "schema omits catalog actions");
    for (const auto name : std::array<std::string_view, 8>{
             "", "Power", " power", "power ", "restart", "exec", "shutdown now", "sh -c reboot"
         })
      rejects("action", std::string(name));
    for (const auto variant :
         std::array<std::string_view, 6>{"default", "primary", "secondary", "outline", "ghost", "destructive"})
      accepts("variant", std::string(variant));
    for (const auto variant : std::array<std::string_view, 4>{"", "Primary", "primary ", "other"})
      rejects("variant", std::string(variant));
    for (const auto key : std::array<std::string_view, 5>{"action", "variant", "glyph", "label", "font_family"}) {
      for (const auto& value : std::vector<greeter::WidgetSettingValue>{
               true, std::int64_t{0}, 0.0, std::vector<std::string>{"power"},
               greeter::WidgetStringMap{{"action", "power"}}
           })
        rejects(key, value);
    }
    accepts("glyph", std::string{});
    accepts("glyph", std::string("power"));
    accepts("label", std::string{});
    accepts("label", std::string("Welcome, 世界"));
    accepts("font_family", std::string{});
    accepts("font_family", std::string("An Uninstalled Font"));
    accepts("color", std::string{});
    for (const auto key : std::array<std::string_view, 2>{"color", "hover_background"}) {
      accepts(key, std::string("primary"));
      accepts(key, std::string("#aabbcc80"));
      rejects(key, std::string("invalid-color"));
      rejects(key, false);
    }
    rejects("hover_background", std::string{});
    accepts("background", true);
    accepts("background", false);
    rejects("background", std::int64_t{1});
    rejects("background", std::string("true"));
    widget.settings = {{"background", false}, {"hover_background", std::string("invalid-color")}};
    require(!greeter::validateWidgetSettings(widget).empty(), "hidden Button color bypassed validation");
    for (const auto key : std::array<std::string_view, 9>{
             "command", "background_color", "background_opacity", "background_radius", "background_padding", "shadow",
             "opacity", "title", "on_click"
         })
      rejects(key, std::string{});
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{false, std::string("reboot")})
      rejects("command", value);

    for (const auto& type : greeter::builtinWidgetDefinitions()) {
      widget = buttonWidget();
      widget.type = type.type;
      require(greeter::widgetSettingDefinitions(type.type).has_value(), "builtin lacks a settings schema");
      require(greeter::validateWidgetSettings(widget).empty(), "builtin omitted settings rejected");
      if (type.kind != greeter::WidgetKind::Button) {
        widget.settings = {{"action", std::string("power")}};
        require(!greeter::validateWidgetSettings(widget).empty(), "Button action leaked into another type");
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-button-settings-test: {}", error.what());
    return 1;
  }
}
