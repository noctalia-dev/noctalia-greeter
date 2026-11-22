#pragma once

#include "greeter/widgets/widget_host.h"
#include "render/scene/node.h"

#include <expected>
#include <memory>
#include <string_view>
#include <variant>
#include <vector>

namespace greeter {

  // One output-local subtree. Its owner supplies logical size with setSize(), lays
  // it out before rendering, and deactivates it before hiding or detaching.
  class WidgetLayer final : public Node {
  public:
    static std::expected<std::unique_ptr<WidgetLayer>, std::vector<WidgetModelIssue>> create(
        const std::vector<WidgetInstance>& instances, std::string_view connector, std::string_view stableIdentifier,
        const WidgetImageContext& images = {}, const SysmonSnapshot* metrics = nullptr,
        const WidgetActions& actions = {}
    );
    ~WidgetLayer() override;

    void setActive(bool active);
    void setMetrics(const SysmonSnapshot& snapshot);

  private:
    WidgetLayer();
    void doLayout(Renderer& renderer) override;
    bool containsLocalPoint(float x, float y, bool includeHitOutset) const override;
    void syncActive();

    struct Entry {
      std::variant<WidgetAnchorPlacement, WidgetRelativePlacement> placement;
      WidgetHost* host;
    };
    std::vector<Entry> m_entries;
    bool m_active = false;
  };

} // namespace greeter
