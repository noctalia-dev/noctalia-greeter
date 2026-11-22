#include "greeter/widgets/widget_config.h"

#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace greeter {
  namespace {
    constexpr std::array<std::string_view, 9> kAnchors{"top-left",    "top-center",    "top-right",
                                                       "center-left", "center",        "center-right",
                                                       "bottom-left", "bottom-center", "bottom-right"};

    void checkKeys(
        const toml::table& table, const std::string& path, std::initializer_list<std::string_view> allowed,
        std::vector<WidgetModelIssue>& issues
    ) {
      for (const auto& [key, node] : table) {
        (void)node;
        if (std::ranges::find(allowed, key.str()) == allowed.end())
          issues.push_back({path + "." + std::string(key.str()), "unknown widget field"});
      }
    }

    template <typename T>
    void readField(
        const toml::table& table, std::string_view key, T& destination, const std::string& path,
        std::vector<WidgetModelIssue>& issues, bool required = false
    ) {
      if (!table.contains(key) && !required)
        return;
      std::optional<T> value;
      if constexpr (std::is_same_v<T, double>) {
        if (table[key].is_number())
          value = table[key].template value<double>();
      } else {
        value = table[key].template value_exact<T>();
      }
      if (value)
        destination = std::move(*value);
      else
        issues.push_back({path + "." + std::string(key), "missing field or incorrect value type"});
    }

    std::vector<WidgetModelIssue> validate(const WidgetCollection& collection) {
      auto issues = validateWidgetStructure(collection);
      if (collection.instances && collection.instances->size() <= kMaxWidgetInstances) {
        for (const auto& widget : *collection.instances) {
          const auto settings = validateWidgetSettings(widget);
          issues.insert(issues.end(), settings.begin(), settings.end());
        }
      }
      return issues;
    }

    WidgetInstance readWidget(const toml::table& table, std::string id, std::vector<WidgetModelIssue>& issues) {
      WidgetInstance widget;
      widget.id = std::move(id);
      const auto path = "widgets.widget." + widget.id;
      readField(table, "type", widget.type, path, issues, true);
      readField(table, "enabled", widget.enabled, path, issues);
      if (table.contains("output")) {
        std::string output;
        readField(table, "output", output, path, issues);
        widget.output = std::move(output);
      }
      readField(table, "box_width", widget.boxWidth, path, issues);
      readField(table, "box_height", widget.boxHeight, path, issues);
      readField(table, "rotation", widget.transform.rotation, path, issues);
      if (const auto normalized = normalizeWidgetRotation(widget.transform.rotation))
        widget.transform.rotation = *normalized;
      readField(table, "flip_x", widget.transform.flipX, path, issues);
      readField(table, "flip_y", widget.transform.flipY, path, issues);
      std::string placement;
      readField(table, "placement", placement, path, issues, true);
      if (placement == "anchor") {
        checkKeys(
            table, path,
            {"type", "enabled", "output", "box_width", "box_height", "rotation", "flip_x", "flip_y", "placement",
             "anchor", "x", "y", "settings"},
            issues
        );
        WidgetAnchorPlacement anchor;
        std::string name;
        readField(table, "anchor", name, path, issues, true);
        const auto* it = std::ranges::find(kAnchors, name);
        if (it == kAnchors.end())
          issues.push_back({path + ".anchor", "expected one of the nine widget anchors"});
        else
          anchor.anchor = static_cast<WidgetAnchor>(std::distance(kAnchors.begin(), it));
        readField(table, "x", anchor.x, path, issues);
        readField(table, "y", anchor.y, path, issues);
        widget.placement = anchor;
      } else if (placement == "relative") {
        checkKeys(
            table, path,
            {"type", "enabled", "output", "box_width", "box_height", "rotation", "flip_x", "flip_y", "placement", "cx",
             "cy", "placement_width", "placement_height", "settings"},
            issues
        );
        WidgetRelativePlacement relative;
        readField(table, "cx", relative.cx, path, issues, true);
        readField(table, "cy", relative.cy, path, issues, true);
        readField(table, "placement_width", relative.placementWidth, path, issues, true);
        readField(table, "placement_height", relative.placementHeight, path, issues, true);
        widget.placement = relative;
      } else {
        issues.push_back({path + ".placement", "expected anchor or relative"});
      }
      if (const auto* settings = table["settings"].as_table()) {
        for (const auto& [key, node] : *settings) {
          std::optional<WidgetSettingValue> value;
          if (const auto boolean = node.value_exact<bool>())
            value = *boolean;
          else if (const auto integer = node.value_exact<std::int64_t>())
            value = *integer;
          else if (const auto floating = node.value_exact<double>())
            value = *floating;
          else if (const auto text = node.value_exact<std::string>())
            value = *text;
          if (value)
            widget.settings.emplace(key.str(), std::move(*value));
          else
            issues.push_back({path + ".settings." + std::string(key.str()), "expected a scalar widget setting"});
        }
      } else if (table.contains("settings")) {
        issues.push_back({path + ".settings", "expected a table"});
      }
      return widget;
    }

    toml::table writeWidget(const WidgetInstance& widget) {
      toml::table table{
          {"type", widget.type},
          {"enabled", widget.enabled},
          {"box_width", widget.boxWidth},
          {"box_height", widget.boxHeight},
          {"rotation", widget.transform.rotation},
          {"flip_x", widget.transform.flipX},
          {"flip_y", widget.transform.flipY}
      };
      if (widget.output)
        table.insert_or_assign("output", *widget.output);
      if (const auto* anchor = std::get_if<WidgetAnchorPlacement>(&widget.placement)) {
        table.insert_or_assign("placement", "anchor");
        table.insert_or_assign("anchor", kAnchors[static_cast<std::size_t>(anchor->anchor)]);
        table.insert_or_assign("x", anchor->x);
        table.insert_or_assign("y", anchor->y);
      } else {
        const auto& relative = std::get<WidgetRelativePlacement>(widget.placement);
        table.insert_or_assign("placement", "relative");
        table.insert_or_assign("cx", relative.cx);
        table.insert_or_assign("cy", relative.cy);
        table.insert_or_assign("placement_width", relative.placementWidth);
        table.insert_or_assign("placement_height", relative.placementHeight);
      }
      if (!widget.settings.empty()) {
        toml::table settings;
        for (const auto& [key, value] : widget.settings) {
          std::visit(
              [&](const auto& scalar) {
                using T = std::decay_t<decltype(scalar)>;
                // Only scalar settings pass the current greeter validators.
                if constexpr (!std::is_same_v<T, std::vector<std::string>> && !std::is_same_v<T, WidgetStringMap>)
                  settings.insert_or_assign(key, scalar);
              },
              value
          );
        }
        table.insert_or_assign("settings", std::move(settings));
      }
      return table;
    }
  } // namespace

  std::expected<std::optional<WidgetCollection>, std::vector<WidgetModelIssue>>
  readWidgetCollection(const toml::table& root) {
    if (!root.contains("widgets"))
      return std::optional<WidgetCollection>{};
    const auto* table = root["widgets"].as_table();
    if (table == nullptr)
      return std::unexpected(std::vector<WidgetModelIssue>{{"widgets", "expected a table"}});
    if (table->empty())
      return std::optional{WidgetCollection{}};
    std::vector<WidgetModelIssue> issues;
    checkKeys(*table, "widgets", {"widget_order", "widget"}, issues);
    const auto* order = (*table)["widget_order"].as_array();
    const auto* records = (*table)["widget"].as_table();
    if (order == nullptr)
      issues.push_back({"widgets.widget_order", "expected an explicit array of widget IDs"});
    if (table->contains("widget") && records == nullptr)
      issues.push_back({"widgets.widget", "expected a table"});
    if (order != nullptr && order->size() > kMaxWidgetInstances)
      issues.push_back({"widgets.widget_order", "at most 64 widget instances are allowed"});
    if (records != nullptr && records->size() > kMaxWidgetInstances)
      issues.push_back({"widgets.widget", "at most 64 widget instances are allowed"});
    if (!issues.empty())
      return std::unexpected(std::move(issues));

    WidgetCollection parsed{std::vector<WidgetInstance>{}};
    if (records != nullptr) {
      for (const auto& [id, node] : *records) {
        if (const auto* record = node.as_table())
          parsed.instances->push_back(readWidget(*record, std::string(id.str()), issues));
        else
          issues.push_back({"widgets.widget." + std::string(id.str()), "expected a widget table"});
      }
    }
    const auto invalid = validate(parsed);
    issues.insert(issues.end(), invalid.begin(), invalid.end());
    WidgetCollection ordered{std::vector<WidgetInstance>{}};
    std::unordered_set<std::string> seen;
    for (const auto& node : *order) {
      const auto id = node.value_exact<std::string>();
      if (!id || !seen.insert(*id).second) {
        issues.push_back({"widgets.widget_order", "expected unique widget ID strings"});
        continue;
      }
      const auto it = std::ranges::find(*parsed.instances, *id, &WidgetInstance::id);
      if (it == parsed.instances->end())
        issues.push_back({"widgets.widget_order", "missing widget record: " + *id});
      else
        ordered.instances->push_back(*it);
    }
    for (const auto& widget : *parsed.instances)
      if (!seen.contains(widget.id))
        issues.push_back({"widgets.widget_order", "unlisted widget record: " + widget.id});
    if (!issues.empty())
      return std::unexpected(std::move(issues));
    return std::optional{std::move(ordered)};
  }

  std::expected<void, std::vector<WidgetModelIssue>>
  writeWidgetCollection(toml::table& root, const std::optional<WidgetCollection>& collection) {
    if (!collection) {
      root.erase("widgets");
      return {};
    }
    auto issues = validate(*collection);
    if (!issues.empty())
      return std::unexpected(std::move(issues));
    toml::table table;
    if (collection->instances) {
      toml::array order;
      toml::table records;
      for (const auto& widget : *collection->instances) {
        order.push_back(widget.id);
        records.insert_or_assign(widget.id, writeWidget(widget));
      }
      table.insert_or_assign("widget_order", std::move(order));
      if (!records.empty())
        table.insert_or_assign("widget", std::move(records));
    }
    root.insert_or_assign("widgets", std::move(table));
    return {};
  }

} // namespace greeter
