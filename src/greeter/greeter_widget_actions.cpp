#include "core/deferred_call.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/power_actions.h"
#include "greeter/ui/greeter_components.h"
#include "greeter/widgets/widget_layer.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/label.h"
#include "ui/style.h"

#include <algorithm>

namespace {
  constexpr Logger kLog("greeter-widgets");
}

greeter::WidgetActions GreeterSurface::widgetActions() {
  return {
      .available = [this](greeter::WidgetAction action) { return widgetActionAvailable(action); },
      .activate = [this](greeter::WidgetAction action, Node& source) { activateWidgetAction(action, source); },
  };
}

bool GreeterSurface::widgetActionAvailable(greeter::WidgetAction action) const {
  using enum greeter::WidgetAction;
  if (m_renderer == nullptr)
    return false;
  switch (action) {
  case None:
    return false;
  case Power:
    return m_powerMenuButton != nullptr;
  case Appearance:
    return m_schemeSelectArea != nullptr && !m_model.schemeNames().empty();
  case Sessions:
    return !m_minimalLogin && m_sessionSelectArea != nullptr && !m_model.sessions().empty();
  case Suspend:
    return power::hasSyncedAction("suspend");
  case Reboot:
    return power::hasSyncedAction("reboot");
  case Shutdown:
    return power::hasSyncedAction("shutdown");
  case Firmware:
    return m_firmwareButton != nullptr;
  }
  return false;
}

bool GreeterSurface::liveWidgetInput(const InputArea* area) const {
  if (!ownsInputArea(area) || !area->enabled() || !area->focusable() || !area->tabStop())
    return false;
  for (const Node* node = area; node != nullptr; node = node->parent())
    if (!node->visible() || !node->hitTestVisible() || node->excludeSubtreeFromTabOrder())
      return false;
  return true;
}

void GreeterSurface::clearWidgetMenuTrigger() {
  m_widgetMenuTrigger = nullptr;
  m_widgetMenuAction = greeter::WidgetAction::None;
}

void GreeterSurface::reconcileWidgetInput() {
  if (auto* focused = m_inputDispatcher.focusedArea(); focused && !liveWidgetInput(focused)) {
    for (const Node* node = focused; node != nullptr; node = node->parent())
      if (dynamic_cast<const greeter::WidgetLayer*>(node)) {
        m_widgetFocusRecovery = true;
        break;
      }
  }
  if (m_widgetMenuTrigger != nullptr && !liveWidgetInput(m_widgetMenuTrigger)) {
    closeMenus();
    m_widgetFocusRecovery = true;
  }
  if (m_widgetFocusRecovery) {
    m_widgetFocusRecovery = false;
    m_inputDispatcher.setFocus(menuReturnFocusTarget());
  }
}

void GreeterSurface::appendWidgetFocusTargets() {
  const auto visit = [this](const auto& self, Node* node, bool widget) -> void {
    if (!node->visible() || !node->hitTestVisible() || node->excludeSubtreeFromTabOrder())
      return;
    widget = widget || dynamic_cast<greeter::WidgetLayer*>(node) != nullptr;
    if (widget) {
      if (auto* area = dynamic_cast<InputArea*>(node); area != nullptr && liveWidgetInput(area)) {
        m_focusRing.push_back({area, [area] { area->dispatchKey(XKB_KEY_Return, 0, 0, true); }});
      }
    }
    for (const auto& child : node->children())
      self(self, child.get(), widget);
  };
  visit(visit, &m_root, false);
}

greeter::LayoutRect GreeterSurface::popupAnchor(const Node* native, greeter::WidgetAction action) const {
  const Node* anchor = m_widgetMenuTrigger != nullptr && m_widgetMenuAction == action ? m_widgetMenuTrigger : native;
  if (anchor == nullptr || !anchor->visible())
    return {};
  float left, top, right, bottom;
  Node::transformedBounds(anchor, left, top, right, bottom);
  (void)Node::mapFromScene(&m_root, left, top, left, top);
  (void)Node::mapFromScene(&m_root, right, bottom, right, bottom);
  left = std::max(0.0F, left);
  top = std::max(0.0F, top);
  right = std::min(m_root.width(), right);
  bottom = std::min(m_root.height(), bottom);
  return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top), right > left && bottom > top};
}

void GreeterSurface::activateWidgetAction(greeter::WidgetAction action, Node& source) {
  using enum greeter::WidgetAction;
  auto* trigger = m_inputDispatcher.firstTabFocusUnder(&source);
  if (!widgetActionAvailable(action) || !liveWidgetInput(trigger))
    return;
  if (m_widgetMenuTrigger == trigger && m_widgetMenuAction == action) {
    if (action == Power)
      closePowerMenu();
    else
      closeMenusAndRestoreFocus(true);
    return;
  }
  closeMenus();
  switch (action) {
  case None:
    return;
  case Power:
    togglePowerMenu();
    break;
  case Appearance:
    toggleSchemeMenu(true);
    break;
  case Sessions:
    toggleSessionMenu();
    break;
  case Suspend:
  case Reboot:
  case Shutdown:
  case Firmware:
    requestPowerAction(action == Suspend ? 1 : action == Reboot ? 2 : action == Shutdown ? 3 : 4);
    return;
  }
  trigger->observeDestruction(m_widgetInputLifetime, [this](InputArea* destroyed) {
    if (m_widgetMenuTrigger != destroyed)
      return;
    closeMenus();
    // Recover after the dispatcher's destruction observer has dropped its references.
    m_widgetFocusRecovery = true;
    const std::weak_ptr<void> lifetime = m_widgetInputLifetime;
    DeferredCall::callLater([this, lifetime] {
      if (!lifetime.expired())
        requestLayout();
    });
  });
  m_widgetMenuTrigger = trigger;
  m_widgetMenuAction = action;
}

void GreeterSurface::setOutputIdentifier(std::string id) {
  if (m_outputIdentifier == id)
    return;
  m_outputIdentifier = std::move(id);
  if (m_renderContext)
    loadPreferences();
  m_widgetsDirty = true;
  requestLayout();
}

void GreeterSurface::setWidgetImages(greeter::WidgetImageContext images, std::string error) {
  m_widgetImages = images;
  m_widgetImageError = std::move(error);
  m_widgetsDirty = true;
  requestLayout();
}

void GreeterSurface::setWidgetsActive(bool active) {
  m_widgetsActive = active;
  if (m_widgetLayer)
    m_widgetLayer->setActive(active);
  if (!active)
    reconcileWidgetInput();
}

greeter::CpuRamDemand GreeterSurface::widgetMetricDemand() const {
  return m_widgetLayer ? m_widgetLayer->metricDemand() : greeter::CpuRamDemand{};
}

void GreeterSurface::setWidgetMetrics(const greeter::SysmonSnapshot& snapshot) {
  if (m_widgetLayer)
    m_widgetLayer->setMetrics(snapshot);
}

void GreeterSurface::syncWidgetLayer() {
  if (!m_widgetsDirty)
    return;
  m_widgetsDirty = false;
  if (m_widgetLayer) {
    m_widgetLayer->setActive(false);
    reconcileWidgetInput();
    m_root.removeChild(m_widgetLayer).reset();
    m_widgetLayer = nullptr;
  }
  m_defaultClockGroup = nullptr;
  m_defaultTimeWidget = nullptr;
  m_defaultDateWidget = nullptr;
  std::string error;
  if (m_widgets && m_widgets->instances) {
    if (!m_widgetImageError.empty()) {
      error = "Widget profile: " + m_widgetImageError;
    } else {
      auto layer = greeter::WidgetLayer::create(
          *m_widgets->instances, m_boundOutputName, m_outputIdentifier, m_widgetImages, nullptr, widgetActions()
      );
      if (layer) {
        m_widgetLayer = layer->get();
        // Custom back-to-front order stays below login, chrome, errors and menus.
        m_widgetLayer->setZIndex(3);
        m_root.addChild(std::move(*layer));
        if (m_placeDefaultClocks) {
          auto group = ui::node({.hitTestVisible = false});
          m_defaultClockGroup = group.get();
          m_widgetLayer->addChild(std::move(group));
          m_defaultTimeWidget = m_widgetLayer->useParentLayout(greeter::kDefaultTimeWidget, *m_defaultClockGroup);
          m_defaultDateWidget = m_widgetLayer->useParentLayout(greeter::kDefaultDateWidget, *m_defaultClockGroup);
          if (m_defaultTimeWidget || m_defaultDateWidget) {
            const auto timeZ = m_defaultTimeWidget ? m_defaultTimeWidget->zIndex() : m_defaultDateWidget->zIndex();
            const auto dateZ = m_defaultDateWidget ? m_defaultDateWidget->zIndex() : timeZ;
            m_defaultClockGroup->setZIndex(std::min(timeZ, dateZ));
          }
        }
      } else {
        const auto& issue = layer.error().front();
        error = "Widget profile: " + issue.path + ": " + issue.message;
        kLog.warn("{}", error);
      }
    }
  }
  const auto message = m_configDiagnosticText + (m_configDiagnosticText.empty() || error.empty() ? "" : "\n") + error;
  m_configErrorLabel->setText(message);
  m_configErrorLabel->setVisible(!message.empty());
  m_configErrorHeading->setVisible(!message.empty());
  m_configErrorBanner->setVisible(!message.empty());
}

float GreeterSurface::measureDefaultClocks(Renderer& renderer, float width, float height) {
  if (!m_defaultClockGroup)
    return 0;
  const bool narrow = width < 1000 || height < 600;
  const float edge = narrow ? std::min(24.0F, std::min(width, height) * .04F) : std::clamp(width * .06F, 64.0F, 128.0F);
  const float available = std::min(m_layoutConfig.components[1].width.value_or(400), width - 2 * edge);
  const auto measure = [&](greeter::WidgetHost* host, std::string_view id, float cap, float inset) {
    if (!host)
      return;
    float fontSize = Style::fontSizeBody * 4;
    for (const auto& widget : *m_widgets->instances) {
      if (widget.id != id)
        continue;
      if (const auto setting = widget.settings.find("font_size"); setting != widget.settings.end()) {
        if (const auto* value = std::get_if<double>(&setting->second); value && *value > 0)
          fontSize = static_cast<float>(*value);
        else if (const auto* integer = std::get_if<std::int64_t>(&setting->second); integer && *integer > 0)
          fontSize = static_cast<float>(*integer);
      }
    }
    float scale = narrow ? std::min(1.0F, cap / fontSize) : 1;
    host->setContentScale(scale);
    host->layout(renderer);
    if (host->width() > std::max(1.0F, available - inset)) {
      scale *= std::max(1.0F, available - inset) / host->width();
      host->setContentScale(scale);
      host->layout(renderer);
    }
  };
  measure(m_defaultTimeWidget, greeter::kDefaultTimeWidget, 56, 0);
  measure(m_defaultDateWidget, greeter::kDefaultDateWidget, 14, 5);
  const float timeHeight = m_defaultTimeWidget ? m_defaultTimeWidget->height() : 0;
  const float dateHeight = m_defaultDateWidget ? m_defaultDateWidget->height() : 0;
  return timeHeight + dateHeight + (timeHeight > 0 && dateHeight > 0 ? 12 : 0);
}

void GreeterSurface::layoutDefaultClocks(const greeter::GreeterLayout& layout) {
  if (!m_defaultClockGroup)
    return;
  const auto& rect = layout[greeter::GreeterComponent::Clock];
  m_defaultClockGroup->setPosition(rect.x, rect.y);
  m_defaultClockGroup->setSize(rect.width, rect.height);
  m_defaultClockGroup->setVisible(rect.visible);
  if (m_defaultTimeWidget) {
    m_defaultTimeWidget->setPosition(0, 0);
    m_defaultTimeWidget->setVisible(rect.visible);
  }
  if (m_defaultDateWidget) {
    const float timeHeight = m_defaultTimeWidget ? m_defaultTimeWidget->height() : 0;
    m_defaultDateWidget->setPosition(5, timeHeight + (timeHeight > 0 ? 12 : 0));
    m_defaultDateWidget->setVisible(rect.visible);
  }
  greeter::applyComponentTransform(*m_defaultClockGroup, rect, layout.transforms[1]);
}
