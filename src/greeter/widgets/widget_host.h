#pragma once

#include "greeter/widgets/widget_model.h"
#include "ui/controls/box.h"

#include <expected>
#include <memory>
#include <vector>

namespace greeter {

  class WidgetContent;

  // One retained presentation per instance/view. The scene coordinator owns placement,
  // output selection, stacking and transforms; this host owns sizing and content lifetime.
  class WidgetHost final : public Box {
  public:
    // Validates before constructing controls. Only implemented presentations can be created.
    static std::expected<std::unique_ptr<WidgetHost>, std::vector<WidgetModelIssue>>
    create(const WidgetInstance& instance);

  private:
    WidgetHost(
        const WidgetInstance& instance, std::unique_ptr<WidgetContent> content, bool background,
        const ColorSpec& backgroundColor, float radius, float padding
    );
    void doLayout(Renderer& renderer) override;

    WidgetContent* m_content;
    float m_boxWidth;
    float m_boxHeight;
    float m_padding;
  };

} // namespace greeter
