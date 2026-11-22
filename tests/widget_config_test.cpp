#include "greeter/widgets/widget_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }

  toml::table fixture() {
    return toml::parse(R"(
[session]
last = 'Umbriel'
[appearance]
scheme = 'Synced'
[widgets]
widget_order = ['submit', 'caption']
[widgets.widget.caption]
type = 'label'
placement = 'anchor'
anchor = 'top-left'
[widgets.widget.caption.settings]
title = 'Lysec Noctalia'
opacity = 0.7
shadow = false
[widgets.widget.submit]
type = 'button'
placement = 'relative'
cx = 1280
cy = 720
placement_width = 2560
placement_height = 1440
box_width = 300
box_height = 64
rotation = -0.25
flip_x = true
output = 'edid:fixture'
[widgets.widget.submit.settings]
label = 'Power'
action = 'power'
)");
  }

  void rejects(const toml::table& root, const std::string& path) {
    const auto result = greeter::readWidgetCollection(root);
    require(
        !result && std::ranges::any_of(result.error(), [&](const auto& issue) { return issue.path == path; }),
        "missing config diagnostic"
    );
  }

  void rejectsWrite(const greeter::WidgetCollection& collection) {
    auto root = fixture();
    const auto before = root;
    require(!greeter::writeWidgetCollection(root, collection), "invalid model serialized");
    require(root == before, "failed write changed unrelated or widget data");
  }
} // namespace

int main() {
  try {
    using namespace greeter;
    auto root = toml::parse("[session]\nlast='Umbriel'\n");
    auto result = readWidgetCollection(root);
    require(result && !*result, "absent collection did not remain unmanaged");
    require(writeWidgetCollection(root, WidgetCollection{}).has_value(), "defaults write failed");
    result = readWidgetCollection(root);
    require(result && *result && !(*result)->instances, "empty table did not request defaults");
    require(root["widgets"].as_table()->empty(), "defaults materialized custom instances");
    const WidgetCollection empty{std::vector<WidgetInstance>{}};
    require(writeWidgetCollection(root, empty).has_value(), "empty collection write failed");
    result = readWidgetCollection(root);
    require(result && *result && (*result)->instances && (*result)->instances->empty(), "empty list became defaults");
    require(
        writeWidgetCollection(root, std::nullopt).has_value() && !root.contains("widgets"), "unmanaged write failed"
    );
    require(root["session"]["last"] == "Umbriel", "widget reset changed session state");

    root = fixture();
    result = readWidgetCollection(root);
    require(result && *result && (*result)->instances->size() == 2, "valid collection rejected");
    const auto collection = **result;
    const auto& button = collection.instances->front();
    require(button.id == "submit" && collection.instances->back().id == "caption", "table order replaced widget_order");
    require(
        button.output == "edid:fixture" && button.transform.flipX && button.transform.rotation == -0.25,
        "output or transforms lost"
    );
    require(std::holds_alternative<WidgetRelativePlacement>(button.placement), "relative placement lost");
    require(!collection.instances->back().settings.contains("description"), "reader materialized omitted settings");
    require(
        std::holds_alternative<double>(collection.instances->back().settings.at("opacity")),
        "double setting changed type"
    );
    auto output =
        toml::parse("[session]\nlast='Other'\n[extension]\nkeep=true\n[widgets.widget.stale]\ntype='label'\n");
    require(writeWidgetCollection(output, collection).has_value(), "valid model write failed");
    require(
        output["session"]["last"] == "Other" && output["extension"]["keep"] == true, "writer changed other sections"
    );
    require(!output["widgets"]["widget"].as_table()->contains("stale"), "writer merged stale widget records");
    std::ostringstream serialized;
    serialized << output;
    auto roundtrip = readWidgetCollection(toml::parse(serialized.str()));
    require(roundtrip && *roundtrip == result.value(), "text serialization changed the typed collection");

    auto all = collection;
    for (unsigned i = 0; i < 9; ++i) {
      auto widget = collection.instances->back();
      widget.id = "anchor-" + std::to_string(i);
      widget.placement = WidgetAnchorPlacement{static_cast<WidgetAnchor>(i), -12.5, 24};
      widget.enabled = i != 0;
      widget.transform = {-std::numbers::pi, i % 2 == 0, i % 3 == 0};
      all.instances->push_back(widget);
    }
    for (const auto* type : {"clock", "sticker", "sysmon"}) {
      WidgetInstance widget;
      widget.id = std::string(type) + "-1";
      widget.type = type;
      if (widget.type == "sysmon")
        widget.settings = {{"label_min_width", std::int64_t{12}}, {"display", std::string("gauge")}};
      all.instances->push_back(widget);
    }
    require(writeWidgetCollection(output, all).has_value(), "complete catalog write failed");
    roundtrip = readWidgetCollection(output);
    require(roundtrip && *roundtrip == std::optional{all}, "catalog, anchors or scalar settings changed");
    require(
        std::holds_alternative<std::int64_t>(roundtrip->value().instances->back().settings.at("label_min_width")),
        "integer setting changed type"
    );
    auto& records = *root["widgets"]["widget"].as_table();
    auto& caption = *records["caption"].as_table();
    caption.insert_or_assign("rotation", 3 * std::numbers::pi);
    roundtrip = readWidgetCollection(root);
    require(
        roundtrip && roundtrip->value().instances->back().transform.rotation == -std::numbers::pi,
        "finite radians did not normalize like Noctalia"
    );

    rejects(toml::parse("widgets=1"), "widgets");
    rejects(toml::parse("[widgets]\nwidget={}\n"), "widgets.widget_order");
    rejects(toml::parse("[widgets]\nwidget_order=[]\nextra=true"), "widgets.extra");
    rejects(toml::parse("[widgets]\nwidget_order=[]\nwidget=[]"), "widgets.widget");
    rejects(toml::parse("[widgets]\nwidget_order=['absent']"), "widgets.widget_order");
    for (const auto* order : {"['submit']", "['submit','submit','caption']", "[true,'caption']", "'submit'"}) {
      auto invalid = fixture();
      invalid["widgets"].as_table()->insert_or_assign(
          "widget_order", *toml::parse(std::string("value=") + order).get("value")
      );
      rejects(invalid, "widgets.widget_order");
    }
    for (const auto& [key, value] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"type", "'calendar'"},
             {"type", "'plugin/widget'"},
             {"enabled", "1"},
             {"output", "''"},
             {"box_width", "true"},
             {"box_height", "15"},
             {"rotation", "inf"},
             {"flip_x", "'true'"},
             {"placement", "'other'"},
             {"anchor", "'other'"},
             {"x", "8193"},
             {"settings", "false"},
             {"cx", "2"}
         }) {
      auto invalid = fixture();
      invalid["widgets"]["widget"]["caption"].as_table()->insert_or_assign(
          key, *toml::parse(std::string("value=") + std::string(value)).get("value")
      );
      rejects(invalid, "widgets.widget.caption." + std::string(key));
    }
    for (const auto* key : {"cx", "cy", "placement_width", "placement_height"}) {
      auto invalid = fixture();
      invalid["widgets"]["widget"]["submit"].as_table()->erase(key);
      rejects(invalid, std::string("widgets.widget.submit.") + key);
    }
    for (const auto* value : {"[]", "{}", "1", "'invalid'"}) {
      auto invalid = fixture();
      invalid["widgets"]["widget"]["caption"]["settings"].as_table()->insert_or_assign(
          "shadow", *toml::parse(std::string("value=") + value).get("value")
      );
      rejects(invalid, "widgets.widget.caption.settings.shadow");
    }
    for (const auto* metric : {"gpu_temp", "gpu_usage", "gpu_vram", "gpu_vram_used", "net_rx"}) {
      auto invalid = output;
      invalid["widgets"]["widget"]["sysmon-1"]["settings"].as_table()->insert_or_assign("stat2", metric);
      rejects(invalid, "widgets.widget.sysmon-1.settings.stat2");
    }
    auto invalid = fixture();
    invalid["widgets"]["widget"]["submit"]["settings"].as_table()->insert_or_assign("command", "echo unsafe");
    rejects(invalid, "widgets.widget.submit.settings.command");
    invalid = fixture();
    invalid["widgets"]["widget"].as_table()->insert_or_assign("not-a-table", true);
    rejects(invalid, "widgets.widget.not-a-table");

    auto maximum = empty;
    for (std::size_t i = 0; i < kMaxWidgetInstances; ++i) {
      auto widget = collection.instances->back();
      widget.id = "w" + std::to_string(i);
      maximum.instances->push_back(widget);
    }
    require(
        writeWidgetCollection(output, maximum).has_value() && readWidgetCollection(output).has_value(),
        "64 widgets rejected"
    );
    output["widgets"]["widget_order"].as_array()->push_back("overflow");
    rejects(output, "widgets.widget_order");
    output["widgets"]["widget_order"].as_array()->pop_back();
    output["widgets"]["widget"].as_table()->insert_or_assign("overflow", toml::table{});
    rejects(output, "widgets.widget");
    maximum.instances->push_back(collection.instances->front());
    rejectsWrite(maximum);
    auto badModel = collection;
    badModel.instances->front().transform.rotation = std::numbers::pi;
    rejectsWrite(badModel);
    badModel = collection;
    badModel.instances->back().id = "login";
    rejectsWrite(badModel);
    badModel = collection;
    badModel.instances->back().settings["opacity"] = std::numeric_limits<double>::quiet_NaN();
    rejectsWrite(badModel);
    badModel = collection;
    badModel.instances->back().settings["title"] = std::vector<std::string>{"unsupported"};
    rejectsWrite(badModel);
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-config-test: {}", error.what());
    return 1;
  }
}
