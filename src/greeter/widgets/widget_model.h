#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace greeter {

  inline constexpr std::size_t kMaxWidgetInstances = 64;

  enum class WidgetAnchor : std::uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
  };

  using WidgetStringMap = std::unordered_map<std::string, std::string>;
  using WidgetSettingValue =
      std::variant<bool, std::int64_t, double, std::string, std::vector<std::string>, WidgetStringMap>;

  struct WidgetAnchorPlacement {
    WidgetAnchor anchor = WidgetAnchor::Center;
    double x = 0;
    double y = 0;
    bool operator==(const WidgetAnchorPlacement&) const = default;
  };

  struct WidgetRelativePlacement {
    double cx = 0;
    double cy = 0;
    double placementWidth = 1920;
    double placementHeight = 1080;
    bool operator==(const WidgetRelativePlacement&) const = default;
  };

  struct WidgetTransform {
    // Canonical stored radians in [-pi, pi), applied around the fitted tile center.
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    bool operator==(const WidgetTransform&) const = default;
  };

  struct WidgetInstance {
    std::string id;
    std::string type;
    bool enabled = true;
    std::variant<WidgetAnchorPlacement, WidgetRelativePlacement> placement;
    // Absence mirrors on active outputs; a nonempty identity pins without retargeting.
    std::optional<std::string> output;
    // Zero requests natural size; configured dimensions use logical pixels.
    double boxWidth = 0;
    double boxHeight = 0;
    WidgetTransform transform;
    std::unordered_map<std::string, WidgetSettingValue> settings;
    bool operator==(const WidgetInstance&) const = default;
  };

  struct WidgetCollection {
    // Unset requests defaults; an engaged vector, including empty, is custom back-to-front order.
    // Configuration owners keep this collection optional: absence leaves widgets unmanaged.
    std::optional<std::vector<WidgetInstance>> instances;
    bool operator==(const WidgetCollection&) const = default;
  };

  struct WidgetModelIssue {
    std::string path;
    std::string message;
  };

  std::optional<double> normalizeWidgetRotation(double radians);
  // Structural checks only. Catalog admission and per-type settings validation are separate.
  std::vector<WidgetModelIssue> validateWidgetStructure(const WidgetCollection& collection);

} // namespace greeter
