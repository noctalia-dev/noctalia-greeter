#include "core/deferred_call.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "greeter/greeter_surface.h"
#include "greeter/power_actions.h"
#include "greeter/widgets/widget_layer.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "ui/controls/button.h"
#include "ui/controls/label.h"

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
  std::string error;
  // Default requests keep the native clock until the coordinated Clock-widget cutover.
  if (m_widgets && m_widgets->instances) {
    auto layer = greeter::WidgetLayer::create(
        *m_widgets->instances, m_boundOutputName, m_outputIdentifier, {}, nullptr, widgetActions()
    );
    if (layer) {
      m_widgetLayer = layer->get();
      // Custom back-to-front order stays below login, chrome, errors and menus.
      m_widgetLayer->setZIndex(3);
      m_root.addChild(std::move(*layer));
    } else {
      const auto& issue = layer.error().front();
      error = "Widget profile: " + issue.path + ": " + issue.message;
      kLog.warn("{}", error);
    }
  }
  const auto message = m_configDiagnosticText + (m_configDiagnosticText.empty() || error.empty() ? "" : "\n") + error;
  m_configErrorLabel->setText(message);
  m_configErrorLabel->setVisible(!message.empty());
  m_configErrorHeading->setVisible(!message.empty());
  m_configErrorBanner->setVisible(!message.empty());
}
