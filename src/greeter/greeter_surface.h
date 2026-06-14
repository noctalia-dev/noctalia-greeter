#pragma once

#include "config/config_types.h"
#include "greetd/greetd_client.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_sessions.h"
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
#include <sys/types.h>
#include <vector>

class Box;
class Button;
class Glyph;
class GreeterWindow;
class Input;
class InputArea;
class Label;
class RenderContext;
class ImageNode;
class RectNode;
class WallpaperNode;

class GreeterSurface {
public:
  GreeterSurface();
  ~GreeterSurface();

  void initialize(RenderContext* context);

  void setWindow(GreeterWindow* window);
  void setGreetdClient(GreetdClient* client);
  void setUsername(const std::string& username);
  void setOnExitRequested(std::function<void()> callback);
  void setOnStateChanged(std::function<void(GreeterSurface*)> callback);

  void mirrorStateFrom(const GreeterSurface& other);

  void onPointerLeave();
  void onPointerEvent(float x, float y, std::uint32_t button, bool pressed);
  void onPointerMotion(float x, float y);
  void onKeyEvent(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit);

  void onThemeChanged();
  [[nodiscard]] bool handleNavigationKey(std::uint32_t sym, std::uint32_t modifiers);
  void requestLayout();
  void requestRedraw();
  void flushDeferredFrameRequests();

  void prepareFrame(std::uint32_t width, std::uint32_t height, bool needsLayout);

  void setOutputViewport(float x, float y, float width, float height);
  void clearOutputViewport();

  [[nodiscard]] Node* sceneRoot() noexcept { return &m_root; }

private:
  void notifyStateChanged();
  [[nodiscard]] bool pointerInViewport(float x, float y) const;
  void syncScaledTypography();
  void layoutScene(std::uint32_t width, std::uint32_t height);
  void tryAuthenticate();
  void onAuthSuccess();
  void onAuthError(const GreetdError& error);
  void resetAuthSession();
  void clearPasswordInput();
  bool driveAuthConversation(std::optional<GreetdAuthMessage> pending);
  void updateStatus(const std::string& text, bool isError);
  void toggleUserMenu();
  void toggleSessionMenu();
  void toggleSchemeMenu();
  void closeMenus();
  void closeMenusAndRestoreFocus();
  void selectSession(std::size_t index);
  void selectScheme(std::size_t index);
  void runBackAction();
  void rebuildFocusRing();
  void applySelectorBoxStyle(Box* box, const InputArea* area);
  void syncPanelSessionChrome();
  [[nodiscard]] float measureIconSelectorWidth(Glyph* icon, Glyph* chevron) const;
  void layoutSelector(Box* box, Glyph* icon, Glyph* chevron, InputArea* area, float x, float y, float w, float h);
  void layoutPanelSessionSelector(float x, float y, float w, float h);
  void layoutPowerButtons(float ox, float oy, float sw, float sh);
  void commitImmediateFrame(bool layout);
  void setFocusIndex(std::ptrdiff_t index);
  void syncFocusIndexFromFocused();
  [[nodiscard]] std::ptrdiff_t defaultFocusIndex() const;
  void moveFocus(int delta);
  void activateFocused();
  [[nodiscard]] bool menuOpen() const noexcept;
  void moveMenuHighlight(int delta);
  void activateMenuHighlight();
  void applyMenuHighlight();
  void buildMenu(
      const std::vector<std::string>& names, std::size_t selected, Box* anchor, bool upward, bool rightAlign, int zBase,
      Box*& panelOut, std::vector<Box*>& rows, std::vector<Label*>& labels, std::vector<InputArea*>& areas,
      std::function<void(std::size_t)> onSelect
  );
  void rebuildUserMenu();
  void rebuildSessionMenu();
  void rebuildSchemeMenu();
  void clearUserMenu();
  void clearSessionMenu();
  void clearSchemeMenu();
  void enterPasswordStep(std::size_t userIndex);
  void loadPreferences();
  void savePreferences() const;
  void buildSchemeNames();
  void applyScheme(std::size_t schemeIndex);
  void clearWallpaperDisplay();
  [[nodiscard]] bool isSyncedScheme(std::size_t schemeIndex) const;
  [[nodiscard]] std::optional<std::size_t> findSchemeIndex(std::string_view name) const;
  void syncWallpaperTexture();
  void syncHeaderUserAvatar(class Renderer& renderer, float size, float panelX, float panelWidth, float headerY);

  Node m_root;
  AnimationManager m_animations;
  GreeterWindow* m_window = nullptr;
  RenderContext* m_renderContext = nullptr;
  GreetdClient* m_greetdClient = nullptr;
  InputDispatcher m_inputDispatcher;
  std::function<void(GreeterSurface*)> m_onStateChanged;

  WallpaperNode* m_wallpaper = nullptr;
  RectNode* m_letterbox = nullptr;
  Node* m_backdrop = nullptr;
  float m_viewportX = 0.0f;
  float m_viewportY = 0.0f;
  float m_viewportWidth = 0.0f;
  float m_viewportHeight = 0.0f;
  bool m_outputViewportActive = false;
  Node* m_brandPane = nullptr;
  ImageNode* m_bottomBrandLogo = nullptr;
  Glyph* m_headerUserGlyph = nullptr;
  ImageNode* m_headerUserAvatar = nullptr;
  Label* m_brandTitleLabel = nullptr;
  Label* m_brandSubtitleLabel = nullptr;
  Label* m_formSubtitleLabel = nullptr;
  Node* m_panelDivider = nullptr;
  Box* m_loginPanel = nullptr;
  Box* m_userSelectBox = nullptr;
  Label* m_userSelectLabel = nullptr;
  Glyph* m_userSelectGlyph = nullptr;
  InputArea* m_userSelectArea = nullptr;
  Box* m_sessionSelectBox = nullptr;
  Glyph* m_sessionSelectIcon = nullptr;
  Label* m_sessionSelectLabel = nullptr;
  Glyph* m_sessionSelectGlyph = nullptr;
  InputArea* m_sessionSelectArea = nullptr;
  Box* m_schemeSelectBox = nullptr;
  Glyph* m_schemeSelectIcon = nullptr;
  Label* m_schemeSelectLabel = nullptr;
  Glyph* m_schemeSelectGlyph = nullptr;
  InputArea* m_schemeSelectArea = nullptr;
  Input* m_passwordField = nullptr;
  Button* m_loginButton = nullptr;
  Button* m_backButton = nullptr;
  Label* m_statusLabel = nullptr;
  Button* m_shutdownButton = nullptr;
  Button* m_rebootButton = nullptr;
  Button* m_firmwareButton = nullptr;
  bool m_canRebootToFirmware = false;
  std::vector<Button*> m_userRowButtons;
  std::vector<Glyph*> m_userRowArrows;

  std::string m_username;
  std::string m_password;
  std::string m_status;
  bool m_statusIsError = false;
  bool m_authenticating = false;
  bool m_authSessionStarted = false;
  std::function<void()> m_onExitRequested;
  TextureHandle m_brandLogoTexture{};
  TextureHandle m_headerAvatarTexture{};
  TextureHandle m_wallpaperTexture{};
  std::string m_loadedHeaderAvatarPath;
  std::string m_wallpaperPath;
  WallpaperFillMode m_wallpaperFillMode = WallpaperFillMode::Crop;
  Color m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  bool m_wallpaperDirty = false;
  bool m_hasSyncedWallpaper = false;
  std::chrono::steady_clock::time_point m_lastAnimTick{};
  bool m_animTickInitialized = false;
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

  std::vector<std::string> m_users;
  std::vector<uid_t> m_userUids;
  std::vector<std::string> m_userIconPaths;
  std::vector<greeter::SessionOption> m_sessions;
  std::size_t m_selectedUser = 0;
  std::size_t m_selectedSession = 0;
  bool m_userMenuOpen = false;
  bool m_sessionMenuOpen = false;
  bool m_schemeMenuOpen = false;
  bool m_passwordVisible = false;
  Box* m_userMenuPanel = nullptr;
  Box* m_sessionMenuPanel = nullptr;
  std::vector<Label*> m_userMenuLabels;
  std::vector<InputArea*> m_userMenuAreas;
  std::vector<Box*> m_userMenuRows;
  std::vector<Label*> m_sessionMenuLabels;
  std::vector<InputArea*> m_sessionMenuAreas;
  std::vector<Box*> m_sessionMenuRows;
  Box* m_schemeMenuPanel = nullptr;
  std::vector<Label*> m_schemeMenuLabels;
  std::vector<InputArea*> m_schemeMenuAreas;
  std::vector<Box*> m_schemeMenuRows;
  std::vector<std::string> m_schemeNames;
  std::size_t m_selectedScheme = 0;
  std::optional<GreeterSyncedAppearance> m_syncedAppearance;

  void loadUsers();
  void loadSessions();
  void refreshSelectionLabels();
};
