#include "greeter/widgets/widget_settings.h"
#include "ui/palette.h"

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

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  greeter::WidgetInstance label() {
    greeter::WidgetInstance widget;
    widget.id = "caption";
    widget.type = "label";
    return widget;
  }
  void accepts(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = label();
    widget.settings.emplace(key, std::move(value));
    require(greeter::validateWidgetSettings(widget).empty(), "valid Label setting rejected");
  }
  void rejects(std::string_view key, greeter::WidgetSettingValue value) {
    auto widget = label();
    widget.settings.emplace(key, std::move(value));
    const auto issues = greeter::validateWidgetSettings(widget);
    const auto path = "widgets.widget.caption.settings." + std::string(key);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing Label setting diagnostic"
    );
  }

  void checkStickerSettings() {
    auto widget = label();
    widget.id = "sticker1";
    widget.type = "sticker";
    const auto definitions = greeter::widgetSettingDefinitions(widget.type);
    require(definitions && definitions->size() == 7, "Sticker schema is incomplete");
    std::set<std::string_view> keys;
    for (const auto& definition : *definitions) {
      require(keys.insert(definition.key).second, "duplicate Sticker setting");
      widget.settings.emplace(definition.key, definition.defaultValue);
    }
    require(greeter::validateWidgetSettings(widget).empty(), "declared Sticker defaults rejected");
    require(
        std::get<std::string>(widget.settings.at("image_path")).empty()
            && std::get<double>(widget.settings.at("opacity")) == 1.0,
        "Sticker defaults diverged from the Noctalia contract"
    );
    widget.settings.clear();
    require(
        greeter::validateWidgetSettings(widget).empty() && widget.settings.empty(),
        "omitted Sticker settings rejected or materialized"
    );
    // Source and staged references are strings here; no file access or asset authorization is implied.
    for (const auto path :
         std::array<std::string_view, 3>{"", "/missing/source image.png", "widget-assets/image.png"}) {
      widget.settings = {{"image_path", std::string(path)}};
      require(greeter::validateWidgetSettings(widget).empty(), "Sticker image reference rejected by string schema");
    }
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             false, std::int64_t{1}, 1.0, std::vector<std::string>{"image.png"},
             greeter::WidgetStringMap{{"path", "image.png"}}
         }) {
      widget.settings = {{"image_path", value}};
      const auto issues = greeter::validateWidgetSettings(widget);
      require(
          std::ranges::any_of(
              issues, [](const auto& issue) { return issue.path == "widgets.widget.sticker1.settings.image_path"; }
          ),
          "missing Sticker image type diagnostic"
      );
    }
    for (const auto& value :
         std::vector<greeter::WidgetSettingValue>{0.0, 1.0, 0.333, std::int64_t{0}, std::int64_t{1}}) {
      widget.settings = {{"opacity", value}};
      require(greeter::validateWidgetSettings(widget).empty(), "valid Sticker opacity rejected");
    }
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             -0.001, 1.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), true,
             std::string("0.5")
         }) {
      widget.settings = {{"opacity", value}};
      require(!greeter::validateWidgetSettings(widget).empty(), "invalid Sticker opacity admitted");
    }
    widget.settings = {{"background", false}, {"background_padding", 12.0}};
    require(!greeter::validateWidgetSettings(widget).empty(), "hidden Sticker background bypassed validation");
    for (const auto key :
         std::array<std::string_view, 7>{"path", "image", "font_family", "title", "shadow", "action", "command"}) {
      widget.settings = {{std::string(key), std::string("unsupported")}};
      require(!greeter::validateWidgetSettings(widget).empty(), "unsupported Sticker setting admitted");
    }
    widget = label();
    widget.settings = {{"image_path", std::string("image.png")}};
    require(!greeter::validateWidgetSettings(widget).empty(), "Sticker image setting leaked into Label schema");
  }
} // namespace

int main() {
  try {
    auto widget = label();
    const auto definitions = greeter::widgetSettingDefinitions("label");
    require(definitions && definitions->size() == 11, "Label must have its six fields and five background fields");
    std::set<std::string_view> keys;
    for (const auto& definition : *definitions) {
      require(keys.insert(definition.key).second, "duplicate setting in the Label schema");
      widget.settings.emplace(definition.key, definition.defaultValue);
    }
    require(greeter::validateWidgetSettings(widget).empty(), "declared Label defaults rejected");
    widget.settings.clear();
    require(
        greeter::validateWidgetSettings(widget).empty() && widget.settings.empty(),
        "omitted Label settings rejected or materialized"
    );

    accepts("title", std::string{});
    accepts("title", std::string("Welcome, 世界"));
    accepts("description", std::string("A literal caption\nwith a second line"));
    accepts("font_family", std::string{});
    accepts("font_family", std::string("A Font Not Installed Here"));
    accepts("background", false);
    accepts("shadow", false);
    for (const auto key : std::array<std::string_view, 2>{"opacity", "background_opacity"}) {
      accepts(key, 0.0);
      accepts(key, 1.0);
      accepts(key, std::int64_t{0});
      accepts(key, std::int64_t{1});
      accepts(key, 0.333);
      rejects(key, -0.001);
      rejects(key, 1.001);
      rejects(key, std::numeric_limits<std::int64_t>::max());
      rejects(key, true);
      rejects(key, std::string("0.5"));
      for (const auto invalid :
           {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()})
        rejects(key, invalid);
    }
    for (const auto key : std::array<std::string_view, 2>{"background_radius", "background_padding"}) {
      accepts(key, std::int64_t{0});
      accepts(key, std::int64_t{32});
      rejects(key, std::int64_t{-1});
      rejects(key, std::int64_t{33});
      rejects(key, std::numeric_limits<std::int64_t>::max());
      rejects(key, 12.0);
      rejects(key, true);
      rejects(key, std::string("12"));
    }
    for (const auto key : std::array<std::string_view, 2>{"color", "background_color"}) {
      for (const auto& role : kColorRoleTokens)
        accepts(key, std::string(role.token));
      for (const auto color :
           std::array<std::string_view, 6>{"#abc", "#abcd", "#ABCDEF", "#ABCDEF80", "abcdef", " ON_SURFACE "})
        accepts(key, std::string(color));
      for (const auto color : std::array<std::string_view, 11>{
               "", " ", "red", "transparent", "#12", "#12345", "#GGGGGG", "primary;exec", "surface_variant", "outline",
               "rgb(1,2,3)"
           })
        rejects(key, std::string(color));
      rejects(key, false);
      rejects(key, std::int64_t{0});
    }
    for (const auto& value : std::vector<greeter::WidgetSettingValue>{
             std::int64_t{0}, 1.0, std::string("true"), std::vector<std::string>{"true"},
             greeter::WidgetStringMap{{"v", "true"}}
         })
      rejects("background", value);
    rejects("shadow", std::int64_t{1});
    rejects("title", false);
    rejects("description", std::vector<std::string>{"caption"});
    rejects("font_family", std::int64_t{12});
    for (const auto key :
         std::array<std::string_view, 8>{"colour", "font", "scale", "date_format", "action", "command", "stat", ""})
      rejects(key, std::string("unsupported"));

    widget.settings = {{"background", false}, {"background_color", std::string("invalid-color")}};
    require(!greeter::validateWidgetSettings(widget).empty(), "hidden background options bypassed validation");
    checkStickerSettings();
    for (const auto type : std::array<std::string_view, 1>{"button"}) {
      widget = label();
      widget.type = type;
      require(!greeter::widgetSettingDefinitions(type), "unfinished schema advertised as complete");
      const auto issues = greeter::validateWidgetSettings(widget);
      require(
          std::ranges::any_of(
              issues, [](const auto& issue) { return issue.path == "widgets.widget.caption.settings"; }
          ),
          "unfinished widget settings admitted"
      );
    }
    widget.type = "calendar";
    const auto issues = greeter::validateWidgetSettings(widget);
    require(
        std::ranges::any_of(issues, [](const auto& issue) { return issue.path == "widgets.widget.caption.type"; }),
        "Calendar bypassed admission during setting validation"
    );
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-settings-test: {}", error.what());
    return 1;
  }
}
