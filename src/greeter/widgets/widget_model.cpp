#include "greeter/widgets/widget_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <unordered_set>

namespace greeter {
  namespace {
    bool validId(std::string_view id) {
      const auto alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
      };
      if (id.empty() || id.size() > 64 || !alnum(id.front()))
        return false;
      for (const char c : id)
        if (!alnum(c) && c != '_' && c != '-')
          return false;
      constexpr std::array<std::string_view, 6> reserved{"login",    "clock",      "branding",
                                                         "keyboard", "appearance", "power"};
      return !std::ranges::contains(reserved, id);
    }
    bool bounded(double value, double min, double max) { return std::isfinite(value) && value >= min && value <= max; }
  } // namespace

  std::optional<double> normalizeWidgetRotation(double radians) {
    if (!std::isfinite(radians))
      return std::nullopt;
    double result = std::fmod(radians, 2 * std::numbers::pi);
    if (result >= std::numbers::pi)
      result -= 2 * std::numbers::pi;
    if (result < -std::numbers::pi)
      result += 2 * std::numbers::pi;
    return result;
  }

  std::vector<WidgetModelIssue> validateWidgetStructure(const WidgetCollection& collection) {
    std::vector<WidgetModelIssue> issues;
    if (!collection.instances)
      return issues;
    if (collection.instances->size() > kMaxWidgetInstances)
      return {{"widgets.widget_order", "at most 64 widget instances are allowed"}};

    std::unordered_set<std::string> ids;
    for (const auto& widget : *collection.instances) {
      const std::string path = "widgets.widget." + widget.id;
      const auto issue = [&](std::string_view key, const char* message) {
        issues.push_back({key.empty() ? path : path + "." + std::string(key), message});
      };
      if (!validId(widget.id))
        issue(
            "",
            "expected a nonreserved ASCII widget ID of 1..64 letters, digits, underscores or hyphens, starting with a "
            "letter or digit"
        );
      if (!ids.insert(widget.id).second)
        issues.push_back({"widgets.widget_order", "duplicate widget instance ID: " + widget.id});
      if (widget.type.empty())
        issue("type", "widget type must be nonempty");
      if (widget.output && widget.output->empty())
        issue("output", "output must be a nonempty connector or stable identity");
      if (const auto* anchor = std::get_if<WidgetAnchorPlacement>(&widget.placement)) {
        if (static_cast<unsigned>(anchor->anchor) > static_cast<unsigned>(WidgetAnchor::BottomRight))
          issue("anchor", "expected one of the nine widget anchors");
        if (!bounded(anchor->x, -8192, 8192))
          issue("x", "anchor offset must be finite and within -8192..8192");
        if (!bounded(anchor->y, -8192, 8192))
          issue("y", "anchor offset must be finite and within -8192..8192");
      } else {
        const auto& relative = std::get<WidgetRelativePlacement>(widget.placement);
        if (!bounded(relative.cx, -65536, 65536))
          issue("cx", "relative center must be finite and within -65536..65536");
        if (!bounded(relative.cy, -65536, 65536))
          issue("cy", "relative center must be finite and within -65536..65536");
        if (!bounded(relative.placementWidth, 1, 65536))
          issue("placement_width", "reference size must be finite and within 1..65536");
        if (!bounded(relative.placementHeight, 1, 65536))
          issue("placement_height", "reference size must be finite and within 1..65536");
      }
      if (widget.boxWidth != 0 && !bounded(widget.boxWidth, 16, 4096))
        issue("box_width", "size must be zero for natural sizing or finite and within 16..4096");
      if (widget.boxHeight != 0 && !bounded(widget.boxHeight, 16, 4096))
        issue("box_height", "size must be zero for natural sizing or finite and within 16..4096");
      if (!std::isfinite(widget.transform.rotation)
          || widget.transform.rotation < -std::numbers::pi
          || widget.transform.rotation >= std::numbers::pi)
        issue("rotation", "stored rotation must be finite normalized radians in [-pi, pi)");
      for (const auto& [key, value] : widget.settings)
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
          issue("settings." + key, "numeric widget setting must be finite");
    }
    return issues;
  }

} // namespace greeter
