#include "greeter/widgets/widget_catalog.h"

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
  greeter::WidgetInstance instance(std::string_view type) {
    greeter::WidgetInstance widget;
    widget.id = "sample";
    widget.type = type;
    return widget;
  }
  void rejects(const greeter::WidgetInstance& widget, std::string_view field) {
    const auto issues = greeter::validateWidgetAdmission(widget);
    const auto path = "widgets.widget." + widget.id + "." + std::string(field);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing widget admission diagnostic"
    );
  }
} // namespace

int main() {
  try {
    for (const auto type : std::array<std::string_view, 5>{"clock", "label", "sticker", "sysmon", "button"}) {
      const auto widget = instance(type);
      require(greeter::validateWidgetStructure({std::vector{widget}}).empty(), "valid widget structure rejected");
      require(greeter::validateWidgetAdmission(widget).empty(), "eligible widget type rejected");
    }
    for (const auto type : std::array<std::string_view, 18>{
             "calendar", "weather", "volume", "media_player", "audio_visualizer", "fancy_audio_visualizer", "login_box",
             "author/plugin:entry", "Clock", " clock", "clock ", "", "not-a-widget", "login", "branding", "keyboard",
             "appearance", "power"
         }) {
      const auto widget = instance(type);
      require(greeter::findBuiltinWidget(type) == nullptr, "unavailable type resolved to a builtin");
      rejects(widget, "type");
    }

    std::set<std::string_view> types;
    std::set<greeter::WidgetKind> kinds;
    for (const auto& definition : greeter::builtinWidgetDefinitions()) {
      require(
          types.insert(definition.type).second && kinds.insert(definition.kind).second,
          "ambiguous builtin widget catalog"
      );
    }
    require(greeter::findBuiltinWidget("label")->providers.empty(), "label requires an unnecessary service");
    for (const auto& [type, provider] : std::array<std::pair<std::string_view, greeter::WidgetProvider>, 4>{{
             {"clock", greeter::WidgetProvider::LocalTime},
             {"sticker", greeter::WidgetProvider::ManagedImages},
             {"sysmon", greeter::WidgetProvider::LocalMetrics},
             {"button", greeter::WidgetProvider::GreeterActions},
         }}) {
      const auto* definition = greeter::findBuiltinWidget(type);
      require(
          definition && definition->providers.size() == 1 && definition->providers.front() == provider,
          "widget provider boundary changed"
      );
    }

    std::set<std::string_view> names;
    std::set<greeter::WidgetAction> actions;
    for (const auto& definition : greeter::widgetActionDefinitions()) {
      require(
          names.insert(definition.name).second && actions.insert(definition.action).second,
          "ambiguous greeter action catalog"
      );
      require(greeter::parseWidgetAction(definition.name) == definition.action, "action did not resolve exactly");
      require(
          definition.needsSessionPicker == (definition.action == greeter::WidgetAction::Sessions),
          "compact session-picker constraint changed"
      );
      auto button = instance("button");
      button.settings["action"] = std::string(definition.name);
      require(greeter::validateWidgetAdmission(button).empty(), "native greeter action rejected");
    }
    require(actions.size() == 8, "native greeter action set changed");
    auto button = instance("button");
    require(
        greeter::validateWidgetAdmission(button).empty() && !button.settings.contains("action"),
        "omitted inert action was rejected or written into the profile"
    );
    for (const auto name : std::array<std::string_view, 9>{
             "", "None", "Power", " power", "power ", "restart", "exec", "shutdown now", "sh -c reboot"
         }) {
      require(!greeter::parseWidgetAction(name), "unknown action acquired an implicit alias");
      button.settings["action"] = std::string(name);
      rejects(button, "settings.action");
    }
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             false, std::int64_t{0}, 0.0, std::vector<std::string>{"power"},
             greeter::WidgetStringMap{{"action", "power"}}
         }) {
      button.settings["action"] = value;
      rejects(button, "settings.action");
    }
    for (const auto type : std::array<std::string_view, 5>{"clock", "label", "sticker", "sysmon", "button"}) {
      auto widget = instance(type);
      widget.settings["command"] = std::string{};
      rejects(widget, "settings.command");
      widget.settings["command"] = std::string("touch /tmp/widget-catalog-must-not-run");
      rejects(widget, "settings.command");
      if (type != "button") {
        widget.settings.erase("command");
        widget.settings["action"] = std::string("power");
        rejects(widget, "settings.action");
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-catalog-test: {}", error.what());
    return 1;
  }
}
