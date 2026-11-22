#pragma once

#include "core/timer_manager.h"
#include "greeter/widgets/cpu_ram_provider.h"
#include "greeter/widgets/widget_actions.h"
#include "greeter/widgets/widget_image_source.h"
#include "greeter/widgets/widget_model.h"
#include "ui/controls/box.h"

#include <expected>
#include <memory>
#include <vector>

namespace greeter {

  class WidgetContent;
  class SysmonSnapshot;

  // One retained presentation per instance/view. The scene coordinator owns placement,
  // output selection, stacking and transforms; this host owns sizing and content lifetime.
  class WidgetHost final : public Box {
  public:
    // Validates before constructing controls. Only implemented presentations can be created.
    static std::expected<std::unique_ptr<WidgetHost>, std::vector<WidgetModelIssue>> create(
        const WidgetInstance& instance, const WidgetImageContext& images = {}, const SysmonSnapshot* metrics = nullptr,
        WidgetActions actions = {}
    );

    // Push validated readings from the view owner; no collectors or timers are started.
    void setContentScale(float scale);
    void setMetrics(const SysmonSnapshot& snapshot);

    // The view owner activates mounted, effectively visible instances and deactivates
    // them before hiding/detaching. Destruction cancels pending ticks automatically.
    void setActive(bool active);
    CpuRamDemand metricDemand() const { return m_active && visible() ? m_metricDemand : CpuRamDemand{}; }

  private:
    WidgetHost(
        const WidgetInstance& instance, std::unique_ptr<WidgetContent> content, bool background,
        const ColorSpec& backgroundColor, float radius, float padding
    );
    void doLayout(Renderer& renderer) override;
    void scheduleTick();
    void refreshTime();

    WidgetContent* m_content;
    float m_boxWidth;
    float m_boxHeight;
    float m_padding;
    float m_contentScale = 1;
    bool m_active = false;
    CpuRamDemand m_metricDemand;
    Timer m_tickTimer;
  };

} // namespace greeter
