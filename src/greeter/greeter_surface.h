#pragma once

#include "config/config_types.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_auth_controller.h"
#include "greeter/greeter_layout.h"
#include "greeter/greeter_model.h"
#include "greeter/greeter_sessions.h"
#include "greeter/render/wallpaper_types.h"
#include "greeter/widgets/cpu_ram_provider.h"
#include "greeter/widgets/widget_actions.h"
#include "greeter/widgets/widget_model.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "render/core/texture_handle.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace greeter {
  class WidgetLayer;
}

class Box;
class Button;
class ContextMenuControl;
class Glyph;
class GreeterWindow;
class Input;
class SearchField;
class Scrollbar;
class InputArea;
class Label;
class RenderContext;
class Renderer;
class ImageNode;
class RectNode;
class WallpaperNode;
class GreeterLoginPanel;
class GreeterChrome;

class GreeterSurface {
public:
  GreeterSurface(GreeterModel& model, GreeterAuthController& auth, GreeterViewId viewId);
  ~GreeterSurface();

  void initialize(RenderContext* context);

  void setWindow(GreeterWindow* window);
  void setBoundOutputName(std::string outputName);
  void setWallpaperSpanParams(const WallpaperSpanParams& params);
  void setKeyboardOwner(bool owner) noexcept;
  void syncModel(bool closeSelectionMenus);
  [[nodiscard]] GreeterViewId viewId() const noexcept { return m_viewId; }

  void onPointerLeave();
  void onPointerEvent(float x, float y, std::uint32_t button, bool pressed);
  void onPointerMotion(float x, float y);
  void onPointerAxis(float x, float y, std::uint32_t axis, float axisLines);
  void onKeyEvent(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit);

  void onThemeChanged();
  void setKeyboardLayoutName(std::string name);
  void setOutputIdentifier(std::string id);
  [[nodiscard]] bool handleNavigationKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers);
  void requestLayout();
  void requestRedraw();
  void flushDeferredFrameRequests();

  void prepareFrame(std::uint32_t width, std::uint32_t height, bool needsLayout);
  // Refreshes the wall-clock labels once per second. Returns true when their
  // contents changed and a new layout is needed.
  [[nodiscard]] bool updateClock();

  [[nodiscard]] Node* sceneRoot() noexcept { return &m_root; }
  [[nodiscard]] bool animating() const noexcept { return m_animations.hasActive(); }

  // Hosts using these callbacks must be destroyed before this view.
  [[nodiscard]] greeter::WidgetActions widgetActions();
  void setWidgetsActive(bool active);
  greeter::CpuRamDemand widgetMetricDemand() const;
  void setWidgetMetrics(const greeter::SysmonSnapshot& snapshot);

private:
  void layoutScene(std::uint32_t width, std::uint32_t height);
  void tryAuthenticate();
  void openUserMenu();
  void toggleSessionMenu();
  void toggleSchemeMenu(bool fromWidget = false);
  void closeMenus();
  void closeMenusAndRestoreFocus(bool restoreWidgetTrigger = false);
  // Password field while the password step is up, else null. Selector menus hand
  // focus back here so typing can resume without a manual Shift+Tab.
  [[nodiscard]] InputArea* menuReturnFocusTarget() const;
  void selectSession(std::size_t index);
  void selectScheme(std::size_t index);
  void runBackAction();
  void rebuildFocusRing();
  void syncPanelSessionChrome();
  void syncPanelUserChrome();
  void layoutPanelUserSelector(float x, float y, float w, float h);
  void layoutPanelSessionSelector(float x, float y, float w, float h);
  void commitImmediateFrame(bool layout);
  void togglePowerMenu();
  void closePowerMenu(bool restoreTriggerFocus = true);
  void requestPowerAction(int action);
  [[nodiscard]] bool widgetActionAvailable(greeter::WidgetAction action) const;
  void activateWidgetAction(greeter::WidgetAction action, Node& source);
  [[nodiscard]] bool liveWidgetInput(const InputArea* area) const;
  void reconcileWidgetInput();
  void appendWidgetFocusTargets();
  void clearWidgetMenuTrigger();
  [[nodiscard]] greeter::LayoutRect popupAnchor(const Node* native, greeter::WidgetAction action) const;
  void layoutPowerMenu(float width, float height);
  bool minimalLogin() const;
  void setFocusIndex(std::ptrdiff_t index);
  void syncFocusIndexFromFocused();
  [[nodiscard]] std::ptrdiff_t defaultFocusIndex() const;
  void moveFocus(int delta);
  void activateFocused();
  [[nodiscard]] bool menuOpen() const noexcept;
  void moveMenuHighlight(int delta);
  void activateMenuHighlight();
  void ensureUserMenuHighlightVisible();
  void scrollUserMenu(int delta);
  [[nodiscard]] bool pointerInUserMenuScrollArea(float x, float y) const;
  struct MenuScroll {
    std::size_t offset = 0;
    std::size_t visibleCount = 0;
    Scrollbar* bar = nullptr;
  };
  [[nodiscard]] MenuScroll* activeMenuScroll();
  void scrollSelectorMenu(int delta);
  void applyMenuHighlight();
  void buildMenu(
      const std::vector<std::string>& names, std::size_t selected, const greeter::LayoutRect& anchor, bool appearance,
      bool upward, bool rightAlign, int zBase, Box*& panelOut, ContextMenuControl*& controlOut, MenuScroll& scroll,
      const std::function<void(std::size_t)>& onSelect
  );
  void rebuildUserMenu();
  void refreshUserMenuRows();
  void layoutUserMenuSearchField(float x, float y, float w, float h);
  void rebuildSessionMenu();
  void rebuildSchemeMenu();
  void clearUserMenu();
  void clearUserMenuRows();
  void clearSessionMenu();
  void clearSchemeMenu();
  void enterPasswordStep(std::size_t userIndex);
  [[nodiscard]] bool handleUserMenuSearchKey(std::uint32_t sym, bool preedit);
  void loadPreferences();
  void syncWidgetLayer();
  void reconcileKeyboardFocus();
  [[nodiscard]] bool ownsInputArea(const InputArea* area) const;
  [[nodiscard]] bool showsUserDropdown() const noexcept;
  void applyScheme(std::size_t schemeIndex);
  void applyConfiguredWallpaper();
  void clearWallpaperDisplay();
  [[nodiscard]] bool isSyncedScheme(std::size_t schemeIndex) const;
  [[nodiscard]] std::optional<std::size_t> findSchemeIndex(std::string_view name) const;
  void syncWallpaperTexture();
  void layoutUserAvatar(Renderer& renderer, float x, float y, float size);

  // Nodes unregister animations while being destroyed, so the manager must
  // outlive the entire scene tree (members are destroyed in reverse order).
  AnimationManager m_animations;
  Node m_root;
  GreeterWindow* m_window = nullptr;
  RenderContext* m_renderContext = nullptr;
  Renderer* m_renderer = nullptr;
  InputDispatcher m_inputDispatcher;
  GreeterModel& m_model;
  GreeterAuthController& m_auth;
  const GreeterViewId m_viewId;
  std::uint64_t m_credentialGeneration = 0;
  std::optional<std::size_t> m_appliedScheme;

  std::unique_ptr<GreeterLoginPanel> m_loginComponent;
  std::unique_ptr<GreeterChrome> m_chromeComponent;
  float m_sceneScale = 1.0F;
  greeter::GreeterLayoutConfig m_layoutConfig;
  std::optional<greeter::WidgetCollection> m_widgets;
  greeter::WidgetLayer* m_widgetLayer = nullptr;
  bool m_widgetsDirty = true;
  bool m_widgetsActive = false;
  std::string m_configDiagnosticText;
  bool m_minimalLogin = false;
  std::string m_keyboardLayout;
  std::string m_outputIdentifier;
  Label* m_sessionCaptionLabel = nullptr;
  std::size_t m_userMenuVisibleRows = 8;
  Label* m_userSecondaryLabel = nullptr;
  Button* m_powerMenuButton = nullptr;
  Button* m_suspendButton = nullptr;
  Box* m_powerPanel = nullptr;
  Label* m_powerTitle = nullptr;
  RectNode* m_powerShadow = nullptr;
  bool m_powerMenuOpen = false;
  WallpaperNode* m_wallpaper = nullptr;
  RectNode* m_backdrop = nullptr;
  ImageNode* m_brandLogo = nullptr;
  Glyph* m_headerUserGlyph = nullptr;
  ImageNode* m_headerUserAvatar = nullptr;
  Label* m_identityLabel = nullptr;
  RectNode* m_panelDivider = nullptr;
  Box* m_loginPanel = nullptr;
  Box* m_userSelectBox = nullptr;
  Label* m_userSelectLabel = nullptr;
  Glyph* m_userSelectGlyph = nullptr;
  InputArea* m_userSelectArea = nullptr;
  Box* m_sessionSelectBox = nullptr;
  Label* m_sessionSelectLabel = nullptr;
  Glyph* m_sessionSelectGlyph = nullptr;
  InputArea* m_sessionSelectArea = nullptr;
  Button* m_schemeSelectButton = nullptr;
  InputArea* m_schemeSelectArea = nullptr;
  Input* m_passwordField = nullptr;
  Button* m_loginButton = nullptr;
  Label* m_statusLabel = nullptr;
  RectNode* m_configErrorBanner = nullptr;
  Label* m_configErrorHeading = nullptr;
  Label* m_configErrorLabel = nullptr;
  Label* m_clockTimeLabel = nullptr;
  Label* m_clockDateLabel = nullptr;
  Button* m_shutdownButton = nullptr;
  Button* m_rebootButton = nullptr;
  Button* m_firmwareButton = nullptr;

  TextureHandle m_brandLogoTexture{};
  TextureHandle m_headerAvatarTexture{};
  TextureHandle m_wallpaperTexture{};
  std::string m_loadedHeaderAvatarPath;
  int m_loadedHeaderAvatarPixelSize = 0;
  std::string m_boundOutputName;
  std::string m_wallpaperPath;
  WallpaperFillMode m_wallpaperFillMode = WallpaperFillMode::Crop;
  Color m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  float m_wallpaperBlur = 0.0f;
  float m_wallpaperTint = 0.0f;
  WallpaperSpanParams m_wallpaperSpanParams;
  bool m_wallpaperDirty = false;
  bool m_hasWallpaper = false;
  bool m_clockEnabled = true;
  std::string m_clockTimeFormat = "{:%H:%M}";
  std::string m_clockDateFormat = "%A, %x";
  float m_clockTimeFontSize = 112.0f;
  float m_clockDateFontSize = 19.0f;
  std::optional<std::int64_t> m_lastClockSecond;
  bool m_inInputDispatch = false;
  bool m_inLayout = false;
  bool m_deferredLayoutRequest = false;
  bool m_deferredRedrawRequest = false;

  struct Focusable {
    InputArea* area = nullptr;
    std::function<void()> activate;
  };
  std::vector<Focusable> m_focusRing;
  std::ptrdiff_t m_focusIndex = -1;
  std::ptrdiff_t m_menuHighlight = -1;
  bool m_initialFocusDone = false;
  bool m_isKeyboardOwner = false;

  bool m_userMenuOpen = false;
  bool m_sessionMenuOpen = false;
  bool m_schemeMenuOpen = false;
  Box* m_userMenuPanel = nullptr;
  SearchField* m_userMenuSearchField = nullptr;
  Label* m_userMenuEmptyLabel = nullptr;
  std::vector<Glyph*> m_userMenuRowIcons;
  std::string m_userMenuSearchQuery;
  std::vector<std::size_t> m_userMenuFilteredIndices;
  std::size_t m_userMenuScrollOffset = 0;
  bool m_userMenuSearchFocusPending = false;
  Box* m_sessionMenuPanel = nullptr;
  MenuScroll m_sessionMenuScroll;
  MenuScroll m_schemeMenuScroll;
  float m_menuScrollRemainder = 0.0f;
  std::vector<Label*> m_userMenuLabels;
  std::vector<InputArea*> m_userMenuAreas;
  std::vector<Box*> m_userMenuRows;
  ContextMenuControl* m_sessionMenuControl = nullptr;
  Box* m_schemeMenuPanel = nullptr;
  ContextMenuControl* m_schemeMenuControl = nullptr;
  std::function<void()> m_pendingMenuActivation;
  std::optional<GreeterSyncedAppearance> m_syncedAppearance;
  std::optional<GreeterWallpaperAppearance> m_wallpaperAppearance;
  std::shared_ptr<void> m_widgetInputLifetime = std::make_shared<int>(0);
  InputArea* m_widgetMenuTrigger = nullptr;
  greeter::WidgetAction m_widgetMenuAction = greeter::WidgetAction::None;
  bool m_widgetFocusRecovery = false;

  void refreshSelectionLabels();
};
