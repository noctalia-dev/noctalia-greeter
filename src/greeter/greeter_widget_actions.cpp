#include "greeter/greeter_surface.h"
#include "greeter/power_actions.h"
#include "render/scene/input_area.h"
#include "ui/controls/button.h"

greeter::WidgetActions GreeterSurface::widgetActions() {
  return {
      .available = [this](greeter::WidgetAction action) { return widgetActionAvailable(action); },
      .activate = [this](greeter::WidgetAction action) { activateWidgetAction(action); },
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
    return m_powerMenuButton != nullptr && m_powerMenuButton->visible();
  case Appearance:
    return m_schemeSelectArea != nullptr && m_schemeSelectArea->visible();
  case Sessions:
    return !m_minimalLogin && m_sessionSelectArea != nullptr && m_sessionSelectArea->visible();
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

void GreeterSurface::activateWidgetAction(greeter::WidgetAction action) {
  using enum greeter::WidgetAction;
  if (!widgetActionAvailable(action))
    return;
  switch (action) {
  case None:
    break;
  case Power:
    togglePowerMenu();
    break;
  case Appearance:
    if (m_powerMenuOpen)
      closePowerMenu(false);
    toggleSchemeMenu();
    break;
  case Sessions:
    if (m_powerMenuOpen)
      closePowerMenu(false);
    toggleSessionMenu();
    break;
  case Suspend:
  case Reboot:
  case Shutdown:
  case Firmware:
    closeMenus();
    requestPowerAction(action == Suspend ? 1 : action == Reboot ? 2 : action == Shutdown ? 3 : 4);
    break;
  }
}
