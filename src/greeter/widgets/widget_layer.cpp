#include "greeter/widgets/widget_layer.h"

#include "core/ui_phase.h"
#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace greeter {
  namespace {
    bool validSize(float width, float height) {
      return std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0;
    }

    bool intersectsOutput(const Node& host, float width, float height) {
      const float cosine = std::abs(std::cos(host.rotation()));
      const float sine = std::abs(std::sin(host.rotation()));
      const float halfWidth = (cosine * host.width() + sine * host.height()) * 0.5F;
      const float halfHeight = (sine * host.width() + cosine * host.height()) * 0.5F;
      const float cx = host.x() + host.width() * 0.5F;
      const float cy = host.y() + host.height() * 0.5F;
      return cx + halfWidth > 0 && cy + halfHeight > 0 && cx - halfWidth < width && cy - halfHeight < height;
    }
  } // namespace

  WidgetLayer::WidgetLayer() { setClipChildren(true); }

  WidgetLayer::~WidgetLayer() { setActive(false); }

  std::expected<std::unique_ptr<WidgetLayer>, std::vector<WidgetModelIssue>> WidgetLayer::create(
      const std::vector<WidgetInstance>& instances, std::string_view connector, std::string_view stableIdentifier,
      const WidgetImageContext& images, const SysmonSnapshot* metrics, const WidgetActions& actions
  ) {
    auto issues = validateWidgetStructure({instances});
    if (!issues.empty())
      return std::unexpected(std::move(issues));
    for (const auto& instance : instances) {
      const auto settingIssues = validateWidgetSettings(instance);
      issues.insert(issues.end(), settingIssues.begin(), settingIssues.end());
    }
    if (!issues.empty())
      return std::unexpected(std::move(issues));

    auto layer = std::unique_ptr<WidgetLayer>(new WidgetLayer);
    for (const auto& instance : instances) {
      if (!instance.enabled
          || (instance.output && *instance.output != connector && *instance.output != stableIdentifier))
        continue;
      auto host = WidgetHost::create(instance, images, metrics, actions);
      if (!host)
        return std::unexpected(std::move(host.error()));
      (*host)->setRotation(static_cast<float>(instance.transform.rotation));
      (*host)->setScale(instance.transform.flipX ? -1.0F : 1.0F, instance.transform.flipY ? -1.0F : 1.0F);
      (*host)->setZIndex(static_cast<std::int32_t>(layer->m_entries.size()));
      auto* retained = host->get();
      layer->addChild(std::move(*host));
      layer->m_entries.push_back({instance.placement, retained});
    }
    return layer;
  }

  void WidgetLayer::doLayout(Renderer& renderer) {
    UiPhaseScope phase(UiPhase::Layout);
    const bool sized = validSize(width(), height());
    const bool narrow = width() < 1000 || height() < 600;
    const float edge =
        narrow ? std::min(24.0F, std::min(width(), height()) * 0.04F) : std::clamp(width() * 0.06F, 64.0F, 128.0F);
    for (const auto& entry : m_entries) {
      auto& host = *entry.host;
      host.layout(renderer);
      if (!sized) {
        host.setVisible(false);
        continue;
      }
      float cx, cy;
      if (const auto* anchor = std::get_if<WidgetAnchorPlacement>(&entry.placement)) {
        const auto index = static_cast<unsigned>(anchor->anchor);
        const auto col = index % 3, row = index / 3;
        cx = col == 0 ? edge + host.width() * 0.5F : col == 2 ? width() - edge - host.width() * 0.5F : width() * 0.5F;
        cy = row == 0  ? edge + host.height() * 0.5F
            : row == 2 ? height() - edge - host.height() * 0.5F
                       : height() * 0.5F;
        cx += static_cast<float>(anchor->x);
        cy += static_cast<float>(anchor->y);
      } else {
        const auto& relative = std::get<WidgetRelativePlacement>(entry.placement);
        cx = static_cast<float>(relative.cx / relative.placementWidth * width());
        cy = static_cast<float>(relative.cy / relative.placementHeight * height());
      }
      host.setPosition(cx - host.width() * 0.5F, cy - host.height() * 0.5F);
      host.setVisible(intersectsOutput(host, width(), height()));
    }
    syncActive();
  }

  void WidgetLayer::syncActive() {
    bool active = m_active && validSize(width(), height());
    for (const Node* node = this; node != nullptr && active; node = node->parent())
      active = node->visible();
    for (const auto& entry : m_entries)
      entry.host->setActive(active && entry.host->visible());
  }

  bool WidgetLayer::containsLocalPoint(float x, float y, bool includeHitOutset) const {
    if (!Node::containsLocalPoint(x, y, includeHitOutset))
      return false;
    // Keep output clipping without intercepting login controls under empty space
    // or passive widgets. The shared hit test applies each host's inverse transform.
    return std::ranges::any_of(m_entries, [x, y](const auto& entry) {
      return Node::hitTestStrict(entry.host, x, y) != nullptr;
    });
  }

  void WidgetLayer::setActive(bool active) {
    uiAssertNotRendering("WidgetLayer::setActive");
    m_active = active;
    syncActive();
  }

  void WidgetLayer::setMetrics(const SysmonSnapshot& snapshot) {
    for (const auto& entry : m_entries)
      entry.host->setMetrics(snapshot);
  }

} // namespace greeter
