#include "greeter/widgets/widget_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  greeter::WidgetInstance caption() {
    greeter::WidgetInstance widget;
    widget.id = "caption-1";
    widget.type = "label";
    widget.settings.emplace("title", std::string("Welcome"));
    widget.settings.emplace("opacity", 0.7);
    return widget;
  }
  void rejects(const greeter::WidgetCollection& collection, std::string_view path) {
    const auto issues = greeter::validateWidgetStructure(collection);
    require(
        std::ranges::any_of(issues, [&](const auto& issue) { return issue.path == path; }),
        "missing structural diagnostic"
    );
  }
} // namespace

int main() {
  try {
    std::optional<greeter::WidgetCollection> unmanaged;
    greeter::WidgetCollection defaults;
    const greeter::WidgetCollection empty{std::vector<greeter::WidgetInstance>{}};
    require(!unmanaged && !defaults.instances && empty.instances->empty(), "collection intent was conflated");
    require(greeter::validateWidgetStructure(defaults).empty(), "defaults rejected");
    require(greeter::validateWidgetStructure(empty).empty(), "explicitly zero widgets rejected");

    auto widget = caption();
    widget.output = "disconnected-output";
    widget.enabled = false;
    widget.placement = greeter::WidgetRelativePlacement{1280, 720, 2560, 1440};
    widget.boxWidth = 400;
    widget.boxHeight = 120;
    widget.transform = {-std::numbers::pi, true, true};
    const greeter::WidgetCollection valid{std::vector{widget, caption()}};
    auto ordered = valid;
    ordered.instances->back().id = "caption-2";
    require(greeter::validateWidgetStructure(ordered).empty(), "valid mirrored/pinned instances rejected");
    require(
        ordered.instances->front() == widget && ordered.instances->back().id == "caption-2",
        "validation altered transforms, pinning or back-to-front order"
    );
    rejects(valid, "widgets.widget_order");

    const auto bad = [&](std::string_view key, auto mutate) {
      auto value = caption();
      mutate(value);
      rejects({std::vector{value}}, "widgets.widget." + value.id + "." + std::string(key));
    };
    for (const std::string id :
         {"", "-leading", "has.dot", "path/name", "login", "clock", "branding", "power", "appearance", "keyboard",
          "caf\xC3\xA9"}) {
      auto value = caption();
      value.id = id;
      rejects({std::vector{value}}, "widgets.widget." + id);
    }
    auto tooLong = caption();
    tooLong.id = std::string(65, 'a');
    rejects({std::vector{tooLong}}, "widgets.widget." + tooLong.id);
    greeter::WidgetCollection maximum{std::vector<greeter::WidgetInstance>{}};
    for (std::size_t i = 0; i < greeter::kMaxWidgetInstances; ++i) {
      auto value = caption();
      value.id = "w" + std::to_string(i);
      maximum.instances->push_back(value);
    }
    require(greeter::validateWidgetStructure(maximum).empty(), "64 widget instances rejected");
    maximum.instances->push_back(caption());
    rejects(maximum, "widgets.widget_order");

    bad("type", [](auto& value) { value.type.clear(); });
    bad("output", [](auto& value) { value.output = ""; });
    bad("anchor", [](auto& value) {
      std::get<greeter::WidgetAnchorPlacement>(value.placement).anchor = static_cast<greeter::WidgetAnchor>(9);
    });
    bad("x", [](auto& value) { std::get<greeter::WidgetAnchorPlacement>(value.placement).x = 8193; });
    bad("y", [](auto& value) { std::get<greeter::WidgetAnchorPlacement>(value.placement).y = -8193; });
    bad("cx", [](auto& value) { value.placement = greeter::WidgetRelativePlacement{65537, 0, 1920, 1080}; });
    bad("cy", [](auto& value) { value.placement = greeter::WidgetRelativePlacement{0, -65537, 1920, 1080}; });
    bad("placement_width", [](auto& value) { value.placement = greeter::WidgetRelativePlacement{0, 0, 0, 1080}; });
    bad("placement_height", [](auto& value) { value.placement = greeter::WidgetRelativePlacement{0, 0, 1920, 65537}; });
    bad("box_width", [](auto& value) { value.boxWidth = 15; });
    bad("box_height", [](auto& value) { value.boxHeight = 4097; });
    bad("rotation", [](auto& value) { value.transform.rotation = std::numbers::pi; });
    for (const auto invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
      bad("x", [=](auto& value) { std::get<greeter::WidgetAnchorPlacement>(value.placement).x = invalid; });
      bad("placement_height",
          [=](auto& value) { value.placement = greeter::WidgetRelativePlacement{0, 0, 1920, invalid}; });
      bad("box_width", [=](auto& value) { value.boxWidth = invalid; });
      bad("rotation", [=](auto& value) { value.transform.rotation = invalid; });
      bad("settings.opacity", [=](auto& value) { value.settings["opacity"] = invalid; });
      require(!greeter::normalizeWidgetRotation(invalid), "nonfinite rotation normalized");
    }
    for (const double turns : {-3.0, -1.0, 1.0, 3.0}) {
      const auto normalized = greeter::normalizeWidgetRotation(turns * std::numbers::pi);
      require(normalized && std::abs(*normalized + std::numbers::pi) < 1e-12, "odd half-turn was not canonical");
    }
    require(
        std::abs(*greeter::normalizeWidgetRotation(8 * std::numbers::pi + 0.25) - 0.25) < 1e-12,
        "rotation lost its fractional turn"
    );

    auto bounds = caption();
    bounds.id = std::string(64, 'a');
    bounds.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::BottomRight, -8192, 8192};
    bounds.boxWidth = 16;
    bounds.boxHeight = 4096;
    require(greeter::validateWidgetStructure({std::vector{bounds}}).empty(), "inclusive anchor/size bounds rejected");
    bounds.placement = greeter::WidgetRelativePlacement{-65536, 65536, 1, 65536};
    require(greeter::validateWidgetStructure({std::vector{bounds}}).empty(), "inclusive relative bounds rejected");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-model-test: {}", error.what());
    return 1;
  }
}
