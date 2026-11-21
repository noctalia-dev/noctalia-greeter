#include "greeter/widgets/widget_host.h"

#include "core/ui_phase.h"
#include "greeter/widgets/label_widget.h"
#include "greeter/widgets/widget_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace greeter {
  namespace {
    constexpr float kMinContentFit = 0.05F;
    constexpr float kMaxContentFit = 20.0F;

    ColorSpec colorSpec(const std::string& token) {
      if (const auto role = colorRoleFromToken(token))
        return colorSpecFromRole(*role);
      Color color;
      if (!tryParseHexColor(token, color))
        throw std::logic_error("unvalidated widget color");
      return fixedColorSpec(color);
    }

    float number(const WidgetSettingValue& value) {
      if (const auto* floating = std::get_if<double>(&value))
        return static_cast<float>(*floating);
      return static_cast<float>(std::get<std::int64_t>(value));
    }
  } // namespace

  std::expected<std::unique_ptr<WidgetHost>, std::vector<WidgetModelIssue>>
  WidgetHost::create(const WidgetInstance& instance) {
    auto issues = validateWidgetStructure({std::vector{instance}});
    const auto settingIssues = validateWidgetSettings(instance);
    issues.insert(issues.end(), settingIssues.begin(), settingIssues.end());
    if (!issues.empty())
      return std::unexpected(std::move(issues));
    if (instance.type != "label")
      return std::unexpected(
          std::vector<WidgetModelIssue>{
              {"widgets.widget." + instance.id + ".type", "presentation is not implemented for this widget type"}
          }
      );

    auto settings = instance.settings;
    for (const auto& field : *widgetSettingDefinitions(instance.type))
      settings.try_emplace(std::string(field.key), field.defaultValue);
    const auto text = [&](const char* key) -> const std::string& { return std::get<std::string>(settings.at(key)); };
    auto content = std::make_unique<LabelWidget>(LabelWidget::Options{
        .title = text("title"),
        .description = text("description"),
        .fontFamily = text("font_family"),
        .color = colorSpec(text("color")),
        .opacity = number(settings.at("opacity")),
        .shadow = std::get<bool>(settings.at("shadow")),
    });
    return std::unique_ptr<WidgetHost>(new WidgetHost(
        instance, std::move(content), std::get<bool>(settings.at("background")),
        scaleAlpha(colorSpec(text("background_color")), number(settings.at("background_opacity"))),
        number(settings.at("background_radius")), number(settings.at("background_padding"))
    ));
  }

  WidgetHost::WidgetHost(
      const WidgetInstance& instance, std::unique_ptr<WidgetContent> content, bool background,
      const ColorSpec& backgroundColor, float radius, float padding
  )
      : m_content(content.get()), m_boxWidth(static_cast<float>(instance.boxWidth)),
        m_boxHeight(static_cast<float>(instance.boxHeight)), m_padding(background ? std::round(padding) : 0.0F) {
    setHitTestVisible(false);
    setVisible(instance.enabled);
    if (background) {
      setFill(backgroundColor);
      setRadius(std::round(radius));
    } else {
      clearFill();
    }
    setClipChildren(background || m_boxWidth > 0.0F || m_boxHeight > 0.0F);
    addChild(std::move(content));
  }

  void WidgetHost::doLayout(Renderer& renderer) {
    UiPhaseScope phase(UiPhase::Layout);
    m_content->layoutAtScale(renderer, 1.0F);
    if (m_boxWidth > 0.0F || m_boxHeight > 0.0F) {
      float fit = kMaxContentFit;
      if (m_boxWidth > 0.0F)
        fit = std::min(fit, std::max(1.0F, m_boxWidth - 2.0F * m_padding) / std::max(1.0F, m_content->width()));
      if (m_boxHeight > 0.0F)
        fit = std::min(fit, std::max(1.0F, m_boxHeight - 2.0F * m_padding) / std::max(1.0F, m_content->height()));
      m_content->layoutAtScale(renderer, std::clamp(fit, kMinContentFit, kMaxContentFit));
    }
    const float width = m_boxWidth > 0.0F ? m_boxWidth : m_content->width() + 2.0F * m_padding;
    const float height = m_boxHeight > 0.0F ? m_boxHeight : m_content->height() + 2.0F * m_padding;
    m_content->setPosition(
        std::round((width - m_content->width()) * 0.5F), std::round((height - m_content->height()) * 0.5F)
    );
    setSize(width, height);
  }

} // namespace greeter
