#include "greeter/greeter_surface.h"

#include "accounts/accounts_icon.h"
#include "core/key_modifiers.h"
#include "core/key_symbols.h"
#include "core/log.h"
#include "core/resource_paths.h"
#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_sessions.h"
#include "greeter/greeter_window.h"
#include "greeter/power_actions.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/render_context.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "render/scene/rect_node.h"
#include "render/scene/wallpaper_node.h"
#include "theme/builtin_palettes.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/search_field.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <json.hpp>
#include <linux/input-event-codes.h>
#include <pwd.h>
#include <sys/types.h>
#include <unordered_set>
#include <wayland-client.h>

namespace {
  constexpr Logger kLog("greeter-surface");
  constexpr float kHeaderUserIconBase = 64.0f;
  constexpr float kHeaderAvatarBorderScale = 2.0f;

  Button::ButtonStateColors
  makePaletteState(ColorRole bg, std::optional<ColorRole> border, ColorRole label, float alpha = 1.0f) {
    return Button::ButtonStateColors{
        colorSpecFromRole(bg, alpha),
        border.has_value() ? colorSpecFromRole(*border, alpha) : clearColorSpec(),
        colorSpecFromRole(label, alpha),
    };
  }

  Button::ButtonPalette powerButtonPalette(ColorRole hover, ColorRole onHover) {
    return Button::ButtonPalette{
        .borderWidth = Style::borderWidth(),
        .normal = makePaletteState(ColorRole::Surface, ColorRole::Outline, ColorRole::OnSurface, 0.78f),
        .hover = makePaletteState(hover, std::nullopt, onHover),
        .pressed = makePaletteState(hover, hover, onHover),
        .disabled = makePaletteState(ColorRole::Surface, ColorRole::Outline, ColorRole::OnSurface, 0.40f),
        .selected = std::nullopt,
    };
  }

  constexpr std::size_t kUserDropdownMaxVisibleRows = 8;
  const float kUserMenuPadding = Style::spaceSm();
  const float kUserMenuRowGap = Style::spaceXs();
  const float kUserMenuRowIconGap = Style::spaceSm();

  [[nodiscard]] bool isTextEditKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers) {
    if (utf32 >= 0x20U && utf32 != 0x7FU) {
      return true;
    }
    if (KeySymbol::isBackspace(sym)
        || KeySymbol::isDelete(sym)
        || KeySymbol::isLeft(sym)
        || KeySymbol::isRight(sym)
        || KeySymbol::isHome(sym)
        || KeySymbol::isEnd(sym)) {
      return true;
    }
    const bool ctrl = (modifiers & KeyMod::Ctrl) != 0;
    return ctrl
        && (sym == 'a'
            || sym == 'A'
            || sym == XKB_KEY_a
            || sym == XKB_KEY_A
            || sym == 'c'
            || sym == 'C'
            || sym == XKB_KEY_c
            || sym == XKB_KEY_C
            || sym == 'v'
            || sym == 'V'
            || sym == XKB_KEY_v
            || sym == XKB_KEY_V
            || sym == 'x'
            || sym == 'X'
            || sym == XKB_KEY_x
            || sym == XKB_KEY_X);
  }

  void appendDummyUsers(std::vector<std::string>& users, std::vector<uid_t>& uids) {
    const char* dummyEnv = std::getenv("NOCTALIA_GREETER_DUMMY_USERS");
    if (dummyEnv == nullptr) {
      return;
    }

    std::size_t count = 15;
    if (dummyEnv[0] != '\0') {
      char* end = nullptr;
      const unsigned long parsed = std::strtoul(dummyEnv, &end, 10);
      if (end != dummyEnv && parsed > 0) {
        count = static_cast<std::size_t>(parsed);
      }
    }

    for (std::size_t i = 0; i < count; ++i) {
      char name[32];
      std::snprintf(name, sizeof(name), "dummy-user-%02zu", i + 1);
      users.push_back(name);
      uids.push_back(static_cast<uid_t>(60'000U + i));
    }
  }

  [[nodiscard]] bool matchesUserFilter(std::string_view user, std::string_view query) {
    if (query.empty()) {
      return true;
    }

    std::string loweredUser;
    loweredUser.reserve(user.size());
    for (const char ch : user) {
      loweredUser.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    std::string loweredQuery;
    loweredQuery.reserve(query.size());
    for (const char ch : query) {
      loweredQuery.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    return loweredUser.find(loweredQuery) != std::string::npos;
  }

  [[nodiscard]] std::vector<std::size_t>
  filterUserIndices(const std::vector<std::string>& users, std::string_view query) {
    std::vector<std::size_t> indices;
    indices.reserve(users.size());
    for (std::size_t i = 0; i < users.size(); ++i) {
      if (matchesUserFilter(users[i], query)) {
        indices.push_back(i);
      }
    }
    return indices;
  }

  bool parseColorWallpaperPath(std::string_view path, Color& out) {
    constexpr std::string_view kPrefix = "color:";
    if (!path.starts_with(kPrefix)) {
      return false;
    }
    return tryParseHexColor(path.substr(kPrefix.size()), out);
  }

} // namespace

GreeterSurface::GreeterSurface() = default;

GreeterSurface::~GreeterSurface() {
  if (m_renderContext != nullptr) {
    if (m_brandLogoTexture.id != 0) {
      m_renderContext->textureManager().unload(m_brandLogoTexture);
    }
    if (m_wallpaperTexture.id != 0) {
      m_renderContext->textureManager().unload(m_wallpaperTexture);
    }
  }
}

void GreeterSurface::initialize(RenderContext* context) {
  m_renderContext = context;

  auto letterbox = std::make_unique<RectNode>();
  m_letterbox = letterbox.get();
  m_letterbox->setZIndex(-1);
  m_letterbox->setHitTestVisible(false);
  m_letterbox->setVisible(false);
  m_letterbox->setStyle(
      RoundedRectStyle{
          .fill = rgba(0.0f, 0.0f, 0.0f, 1.0f),
          .fillMode = FillMode::Solid,
      }
  );
  m_root.addChild(std::move(letterbox));

  auto wallpaper = std::make_unique<WallpaperNode>();
  m_wallpaper = wallpaper.get();
  m_wallpaper->setZIndex(0);
  m_root.addChild(std::move(wallpaper));

  auto backdrop = std::make_unique<RectNode>();
  backdrop->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Surface),
          .fillMode = FillMode::Solid,
      }
  );
  m_backdrop = backdrop.get();
  m_backdrop->setZIndex(1);
  m_root.addChild(std::move(backdrop));

  auto bottomLogo = std::make_unique<ImageNode>();
  bottomLogo->setFitMode(ImageFitMode::Contain);
  bottomLogo->setHitTestVisible(false);
  bottomLogo->setZIndex(2);
  m_bottomBrandLogo = bottomLogo.get();
  m_root.addChild(std::move(bottomLogo));

  auto formSubtitle = std::make_unique<Label>();
  formSubtitle->setFontSize(Style::fontSizeTitle());
  formSubtitle->setBold(true);
  formSubtitle->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_formSubtitleLabel = formSubtitle.get();
  m_formSubtitleLabel->setZIndex(6);
  m_root.addChild(std::move(formSubtitle));

  auto brandPane = std::make_unique<RectNode>();
  brandPane->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Surface, 0.95f),
          .fillMode = FillMode::Solid,
      }
  );
  m_brandPane = brandPane.get();
  m_brandPane->setZIndex(5);
  m_root.addChild(std::move(brandPane));

  auto headerGlyph = std::make_unique<Glyph>();
  headerGlyph->setGlyph("user");
  headerGlyph->setGlyphSize(Style::scaled(kHeaderUserIconBase));
  headerGlyph->setColor(colorForRole(ColorRole::OnSurface));
  headerGlyph->setHitTestVisible(false);
  m_headerUserGlyph = headerGlyph.get();
  m_headerUserGlyph->setZIndex(7);
  m_root.addChild(std::move(headerGlyph));

  auto headerAvatar = std::make_unique<ImageNode>();
  headerAvatar->setFitMode(ImageFitMode::Cover);
  headerAvatar->setHitTestVisible(false);
  headerAvatar->setVisible(false);
  m_headerUserAvatar = headerAvatar.get();
  m_headerUserAvatar->setZIndex(7);
  m_root.addChild(std::move(headerAvatar));

  auto brandTitle = std::make_unique<Label>();
  brandTitle->setText("");
  brandTitle->setFontSize(30.0f);
  brandTitle->setBold(true);
  brandTitle->setColor(colorForRole(ColorRole::OnSurface));
  m_brandTitleLabel = brandTitle.get();
  m_brandTitleLabel->setZIndex(7);
  m_root.addChild(std::move(brandTitle));

  auto brandSubtitle = std::make_unique<Label>();
  brandSubtitle->setText("");
  brandSubtitle->setFontSize(12.0f);
  brandSubtitle->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_brandSubtitleLabel = brandSubtitle.get();
  m_brandSubtitleLabel->setZIndex(7);
  m_root.addChild(std::move(brandSubtitle));

  auto divider = std::make_unique<RectNode>();
  divider->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Outline, 0.90f),
          .fillMode = FillMode::Solid,
          .radius = 1.0f,
      }
  );
  m_panelDivider = divider.get();
  m_root.addChild(std::move(divider));

  auto panel = std::make_unique<Box>();
  m_loginPanel = panel.get();
  m_loginPanel->setZIndex(1);
  m_root.addChild(std::move(panel));

  auto pwField = std::make_unique<Input>();
  pwField->setPlaceholder("Type password");
  pwField->setPasswordMode(true);
  pwField->setControlHeight(Style::controlHeight());
  pwField->setOnChange([this](const std::string& value) { m_password = value; });
  pwField->setOnSubmit([this](const std::string&) { tryAuthenticate(); });
  m_passwordField = pwField.get();
  m_passwordField->setZIndex(6);
  m_root.addChild(std::move(pwField));

  auto userBox = std::make_unique<Box>();
  m_userSelectBox = userBox.get();
  m_userSelectBox->setZIndex(6);
  m_root.addChild(std::move(userBox));

  auto userIcon = std::make_unique<Glyph>();
  userIcon->setGlyph("user");
  userIcon->setGlyphSize(Style::fontSizeBody());
  userIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  userIcon->setHitTestVisible(false);
  m_userSelectIcon = userIcon.get();
  m_userSelectIcon->setZIndex(6);
  m_root.addChild(std::move(userIcon));

  auto userLabel = std::make_unique<Label>();
  userLabel->setFontSize(Style::fontSizeBody());
  m_userSelectLabel = userLabel.get();
  m_userSelectLabel->setZIndex(6);
  m_root.addChild(std::move(userLabel));

  auto userGlyph = std::make_unique<Glyph>();
  userGlyph->setGlyph("chevron-down");
  userGlyph->setGlyphSize(14.0f);
  m_userSelectGlyph = userGlyph.get();
  m_userSelectGlyph->setZIndex(6);
  m_root.addChild(std::move(userGlyph));

  auto userArea = std::make_unique<InputArea>();
  userArea->setFocusable(true);
  userArea->setOnPress([this](const InputArea::PointerData& data) {
    if (!data.pressed || data.button != BTN_LEFT || m_userMenuOpen) {
      return;
    }
    openUserMenu();
  });
  m_userSelectArea = userArea.get();
  m_userSelectArea->setZIndex(7);
  m_userSelectArea->setOnFocusChange([this](bool) {
    syncPanelUserChrome();
    requestRedraw();
  });
  m_root.addChild(std::move(userArea));

  auto userSearch = std::make_unique<SearchField>();
  userSearch->setPlaceholder("Search users…");
  userSearch->setFontSize(Style::fontSizeBody());
  userSearch->setVisible(false);
  userSearch->setOnChange([this](const std::string& value) {
    if (value == m_userMenuSearchQuery) {
      return;
    }
    m_userMenuSearchQuery = value;
    m_userMenuScrollOffset = 0;
    refreshUserMenuRows();
  });
  userSearch->setOnKeyDown([this](
                               std::uint32_t sym, std::uint32_t /*utf32*/, std::uint32_t /*modifiers*/, bool preedit
                           ) { return handleUserMenuSearchKey(sym, preedit); });
  m_userMenuSearchField = userSearch.get();
  m_userMenuSearchField->setZIndex(8);
  m_root.addChild(std::move(userSearch));

  auto sessionBox = std::make_unique<Box>();
  m_sessionSelectBox = sessionBox.get();
  m_sessionSelectBox->setZIndex(6);
  m_root.addChild(std::move(sessionBox));

  auto sessionIcon = std::make_unique<Glyph>();
  sessionIcon->setGlyph("device-desktop");
  sessionIcon->setGlyphSize(Style::fontSizeBody());
  sessionIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  sessionIcon->setHitTestVisible(false);
  m_sessionSelectIcon = sessionIcon.get();
  m_sessionSelectIcon->setZIndex(6);
  m_root.addChild(std::move(sessionIcon));

  auto sessionLabel = std::make_unique<Label>();
  sessionLabel->setFontSize(Style::fontSizeBody());
  m_sessionSelectLabel = sessionLabel.get();
  m_sessionSelectLabel->setZIndex(6);
  m_root.addChild(std::move(sessionLabel));

  auto sessionGlyph = std::make_unique<Glyph>();
  sessionGlyph->setGlyph("chevron-down");
  sessionGlyph->setGlyphSize(14.0f);
  m_sessionSelectGlyph = sessionGlyph.get();
  m_sessionSelectGlyph->setZIndex(6);
  m_root.addChild(std::move(sessionGlyph));

  auto sessionArea = std::make_unique<InputArea>();
  sessionArea->setFocusable(true);
  sessionArea->setOnClick([this](const InputArea::PointerData& data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    toggleSessionMenu();
  });
  sessionArea->setOnEnter([this](const InputArea::PointerData&) {
    syncPanelSessionChrome();
    requestRedraw();
  });
  sessionArea->setOnLeave([this]() {
    syncPanelSessionChrome();
    requestRedraw();
  });
  m_sessionSelectArea = sessionArea.get();
  m_sessionSelectArea->setZIndex(7);
  m_sessionSelectArea->setOnFocusChange([this](bool) {
    syncPanelSessionChrome();
    requestRedraw();
  });
  m_root.addChild(std::move(sessionArea));

  auto schemeBox = std::make_unique<Box>();
  m_schemeSelectBox = schemeBox.get();
  m_schemeSelectBox->setZIndex(6);
  m_root.addChild(std::move(schemeBox));

  auto schemeIcon = std::make_unique<Glyph>();
  schemeIcon->setGlyph("palette");
  schemeIcon->setGlyphSize(Style::fontSizeBody());
  schemeIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  schemeIcon->setHitTestVisible(false);
  m_schemeSelectIcon = schemeIcon.get();
  m_schemeSelectIcon->setZIndex(6);
  m_root.addChild(std::move(schemeIcon));

  auto schemeLabel = std::make_unique<Label>();
  schemeLabel->setFontSize(Style::fontSizeBody());
  m_schemeSelectLabel = schemeLabel.get();
  m_schemeSelectLabel->setZIndex(6);
  m_root.addChild(std::move(schemeLabel));

  auto schemeGlyph = std::make_unique<Glyph>();
  schemeGlyph->setGlyph("chevron-down");
  schemeGlyph->setGlyphSize(14.0f);
  m_schemeSelectGlyph = schemeGlyph.get();
  m_schemeSelectGlyph->setZIndex(6);
  m_root.addChild(std::move(schemeGlyph));

  auto schemeArea = std::make_unique<InputArea>();
  schemeArea->setFocusable(true);
  schemeArea->setOnClick([this](const InputArea::PointerData& data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    toggleSchemeMenu();
  });
  m_schemeSelectArea = schemeArea.get();
  m_schemeSelectArea->setZIndex(7);
  m_schemeSelectArea->setOnFocusChange([this](bool) {
    applySelectorBoxStyle(m_schemeSelectBox, m_schemeSelectArea);
    requestRedraw();
  });
  m_root.addChild(std::move(schemeArea));

  auto loginBtn = std::make_unique<Button>();
  loginBtn->setGlyph("arrow-right");
  loginBtn->setGlyphSize(16.0f);
  loginBtn->setVariant(ButtonVariant::Primary);
  loginBtn->setContentAlign(ButtonContentAlign::Center);
  loginBtn->setOnClick([this]() { tryAuthenticate(); });
  m_loginButton = loginBtn.get();
  m_loginButton->setZIndex(6);
  m_loginButton->setAnimationManager(nullptr);
  m_root.addChild(std::move(loginBtn));

  auto backBtn = std::make_unique<Button>();
  backBtn->setGlyph("arrow-left");
  backBtn->setGlyphSize(16.0f);
  backBtn->setCustomPalette(
      Button::ButtonPalette{
          .borderWidth = Style::borderWidth(),
          .normal = makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline, ColorRole::OnSurface),
          .hover = makePaletteState(ColorRole::Secondary, std::nullopt, ColorRole::OnSecondary),
          .pressed = makePaletteState(ColorRole::Primary, ColorRole::Primary, ColorRole::OnPrimary),
          .disabled = makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline, ColorRole::OnSurface, 0.55f),
          .selected = std::nullopt,
      }
  );
  backBtn->setContentAlign(ButtonContentAlign::Center);
  backBtn->setOnPress([this](float /*localX*/, float /*localY*/, bool pressed) {
    if (pressed) {
      runBackAction();
    }
  });
  m_backButton = backBtn.get();
  m_backButton->setZIndex(6);
  m_backButton->setAnimationManager(nullptr);
  m_root.addChild(std::move(backBtn));

  auto status = std::make_unique<Label>();
  status->setFontSize(Style::fontSizeCaption());
  status->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_statusLabel = status.get();
  m_statusLabel->setZIndex(6);
  m_root.addChild(std::move(status));

  m_canRebootToFirmware = power::canRebootToFirmwareSetup();

  const auto makePowerButton =
      [this](std::string_view icon, ColorRole hover, ColorRole onHover, std::function<void()> action) -> Button* {
    auto btn = std::make_unique<Button>();
    btn->setGlyph(icon);
    btn->setGlyphSize(18.0f);
    btn->setContentAlign(ButtonContentAlign::Center);
    btn->setCustomPalette(powerButtonPalette(hover, onHover));
    btn->setOnClick(std::move(action));
    btn->setZIndex(8);
    Button* ptr = btn.get();
    m_root.addChild(std::move(btn));
    return ptr;
  };

  m_shutdownButton = makePowerButton("power", ColorRole::Error, ColorRole::OnError, []() { power::powerOff(); });
  m_shutdownButton->setTooltip("Shut down");
  m_rebootButton = makePowerButton("reload", ColorRole::Secondary, ColorRole::OnSecondary, []() { power::reboot(); });
  m_rebootButton->setTooltip("Restart");
  if (m_canRebootToFirmware) {
    m_firmwareButton =
        makePowerButton("cpu", ColorRole::Secondary, ColorRole::OnSecondary, []() { power::rebootToFirmwareSetup(); });
    m_firmwareButton->setTooltip("Restart to UEFI firmware setup");
  }

  const auto applySyncedPowerButton = [](Button* button, std::string_view action, std::string_view fallbackTooltip) {
    if (button == nullptr) {
      return;
    }
    button->setVisible(power::hasSyncedAction(action));
    if (const auto glyph = power::syncedActionGlyph(action)) {
      button->setGlyph(*glyph);
    }
    if (const auto label = power::syncedActionLabel(action)) {
      button->setTooltip(*label);
    } else {
      button->setTooltip(fallbackTooltip);
    }
  };
  applySyncedPowerButton(m_shutdownButton, "shutdown", "Shut down");
  applySyncedPowerButton(m_rebootButton, "reboot", "Restart");

  m_root.setAnimationManager(&m_animations);
  // Keep state changes immediate to avoid hover flicker.
  for (Button* btn : {m_shutdownButton, m_rebootButton, m_firmwareButton}) {
    if (btn != nullptr) {
      btn->setAnimationManager(nullptr);
    }
  }
  m_inputDispatcher.setSceneRoot(&m_root);
  m_inputDispatcher.setCursorShapeCallback([](std::uint32_t serial, std::uint32_t shape) {
    (void)serial;
    (void)shape;
  });

  loadUsers();
  loadSessions();
  buildSchemeNames();
  loadPreferences();
  if (m_selectedScheme >= m_schemeNames.size()) {
    if (const auto fallback = findSchemeIndex("Noctalia")) {
      m_selectedScheme = *fallback;
    } else {
      m_selectedScheme = 0;
    }
  }
  applyScheme(m_selectedScheme);
  refreshSelectionLabels();
  applyInitialUserSelection();

  if (!m_hideLogo) {
    const auto logoPath = paths::assetPath("noctalia.svg");
    m_brandLogoTexture = m_renderContext->textureManager().loadFromFile(logoPath.string(), 1024, true);
    if (m_bottomBrandLogo != nullptr && m_brandLogoTexture.id != 0) {
      m_bottomBrandLogo->setTextureId(m_brandLogoTexture.id);
      m_bottomBrandLogo->setTextureSize(m_brandLogoTexture.width, m_brandLogoTexture.height);
      m_bottomBrandLogo->setTint(colorForRole(ColorRole::OnSurface, 0.90f));
    } else {
      kLog.warn("failed loading logo texture from {}", logoPath.string());
    }
  }

  requestLayout();
}

void GreeterSurface::applyInitialUserSelection() {
  const auto initialUser = greeter::resolveInitialUserName(greeter::loadGreeterPreferences());
  if (initialUser.has_value()) {
    for (std::size_t i = 0; i < m_users.size(); ++i) {
      if (m_users[i] == *initialUser) {
        m_selectedUser = i;
        setUsername(m_users[i]);
        m_passwordVisible = true;
        m_password.clear();
        if (m_passwordField != nullptr) {
          m_passwordField->setValue("");
        }
        return;
      }
    }

    m_passwordVisible = false;
    kLog.warn("default_user '{}' is not in the greeter user list", *initialUser);
    return;
  }

  if (m_users.size() == 1) {
    m_selectedUser = 0;
    setUsername(m_users.front());
    m_passwordVisible = true;
    m_password.clear();
    if (m_passwordField != nullptr) {
      m_passwordField->setValue("");
    }
    return;
  }

  m_passwordVisible = false;
}

bool GreeterSurface::showsUserDropdown() const noexcept { return m_users.size() > 1; }

void GreeterSurface::setKeyboardOwner(const bool owner) noexcept {
  m_isKeyboardOwner = owner;
  if (owner) {
    reconcileKeyboardFocus();
  }
}

bool GreeterSurface::ownsInputArea(const InputArea* area) const {
  if (area == nullptr) {
    return false;
  }
  for (const Node* node = area; node != nullptr; node = node->parent()) {
    if (node == &m_root) {
      return true;
    }
  }
  return false;
}

void GreeterSurface::reconcileKeyboardFocus() {
  InputArea* focused = InputArea::getFocused();
  if (focused != nullptr && ownsInputArea(focused)) {
    syncFocusIndexFromFocused();
    return;
  }

  if (m_passwordVisible && m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
    syncFocusIndexFromFocused();
    return;
  }

  if (!m_focusRing.empty()) {
    setFocusIndex(defaultFocusIndex());
  }
}

void GreeterSurface::setWindow(GreeterWindow* window) { m_window = window; }

void GreeterSurface::setGreetdClient(GreetdClient* client) { m_greetdClient = client; }

void GreeterSurface::setUsername(const std::string& username) { m_username = username; }

void GreeterSurface::setOnExitRequested(std::function<void()> callback) { m_onExitRequested = std::move(callback); }

void GreeterSurface::setOnStateChanged(std::function<void(GreeterSurface*)> callback) {
  m_onStateChanged = std::move(callback);
}

void GreeterSurface::notifyStateChanged() {
  if (m_onStateChanged) {
    m_onStateChanged(this);
  }
}

void GreeterSurface::mirrorStateFrom(const GreeterSurface& other) {
  if (&other == this) {
    return;
  }

  const bool schemeChanged = m_selectedScheme != other.m_selectedScheme;

  m_selectedUser = other.m_selectedUser;
  m_selectedSession = other.m_selectedSession;
  m_selectedScheme = other.m_selectedScheme;
  m_passwordVisible = other.m_passwordVisible;
  m_username = other.m_username;
  m_status = other.m_status;
  m_statusIsError = other.m_statusIsError;

  if (schemeChanged && m_selectedScheme < m_schemeNames.size()) {
    applyScheme(m_selectedScheme);
  }

  closeMenus();
  refreshSelectionLabels();
  commitImmediateFrame(true);
}

void GreeterSurface::setOutputViewport(float x, float y, float width, float height) {
  m_viewportX = x;
  m_viewportY = y;
  m_viewportWidth = std::max(width, 1.0f);
  m_viewportHeight = std::max(height, 1.0f);
  m_outputViewportActive = true;
  requestLayout();
}

void GreeterSurface::clearOutputViewport() {
  if (!m_outputViewportActive) {
    return;
  }
  m_outputViewportActive = false;
  requestLayout();
}

bool GreeterSurface::pointerInViewport(float x, float y) const {
  if (!m_outputViewportActive) {
    return true;
  }
  return x >= m_viewportX
      && y >= m_viewportY
      && x < m_viewportX + m_viewportWidth
      && y < m_viewportY + m_viewportHeight;
}

void GreeterSurface::onPointerLeave() {
  m_inputDispatcher.pointerLeave();
  requestRedraw();
}

void GreeterSurface::onPointerEvent(float x, float y, std::uint32_t button, bool pressed) {
  if (!pointerInViewport(x, y)) {
    return;
  }
  if (pressed && button == BTN_LEFT && (m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen)) {
    const auto inRect = [x, y](const Node* node) {
      if (node == nullptr) {
        return false;
      }
      return x >= node->x() && y >= node->y() && x <= node->x() + node->width() && y <= node->y() + node->height();
    };

    const bool onUserAnchor = inRect(m_userSelectBox);
    const bool onSessionAnchor = inRect(m_sessionSelectBox);
    const bool onSchemeAnchor = inRect(m_schemeSelectBox);
    const bool onUserMenu = inRect(m_userMenuPanel);
    const bool onSessionMenu = inRect(m_sessionMenuPanel);
    const bool onSchemeMenu = inRect(m_schemeMenuPanel);
    if (!onUserAnchor && !onSessionAnchor && !onSchemeAnchor && !onUserMenu && !onSessionMenu && !onSchemeMenu) {
      closeMenus();
    }
  }

  m_inInputDispatch = true;
  m_inputDispatcher.pointerButton(x, y, button, pressed);
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onPointerMotion(float x, float y) {
  if (!pointerInViewport(x, y)) {
    return;
  }
  m_inInputDispatch = true;
  m_inputDispatcher.pointerMotion(x, y, 0);
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onPointerAxis(float x, float y, std::uint32_t axis, float axisLines) {
  if (!pointerInViewport(x, y) || axisLines == 0.0f || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
    return;
  }
  if (!m_userMenuOpen || !pointerInUserMenuScrollArea(x, y)) {
    return;
  }

  const int delta = static_cast<int>(std::round(axisLines));
  if (delta == 0) {
    return;
  }
  scrollUserMenu(delta);
}

void GreeterSurface::onKeyEvent(
    std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit
) {
  if (!pressed)
    return;
  reconcileKeyboardFocus();
  m_inInputDispatch = true;
  if (!handleNavigationKey(sym, utf32, modifiers)) {
    m_inputDispatcher.keyEvent(sym, utf32, modifiers, pressed, preedit);
  }
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onThemeChanged() { requestLayout(); }

void GreeterSurface::requestLayout() {
  if (m_inInputDispatch) {
    m_deferredLayoutRequest = true;
    return;
  }
  // Requests issued while a layout is already running (the window renders
  // synchronously) would recurse; the in-progress pass already covers them.
  if (m_inLayout) {
    return;
  }
  if (m_window != nullptr) {
    m_window->requestLayout();
  }
}

void GreeterSurface::requestRedraw() {
  if (m_inInputDispatch) {
    m_deferredRedrawRequest = true;
    return;
  }
  if (m_inLayout) {
    return;
  }
  if (m_window != nullptr) {
    m_window->requestRedraw();
  }
}

void GreeterSurface::flushDeferredFrameRequests() {
  if (m_window == nullptr) {
    m_deferredLayoutRequest = false;
    m_deferredRedrawRequest = false;
    return;
  }
  const bool layout = m_deferredLayoutRequest;
  const bool redraw = m_deferredRedrawRequest;
  m_deferredLayoutRequest = false;
  m_deferredRedrawRequest = false;
  if (layout) {
    m_window->requestLayout();
  } else if (redraw) {
    m_window->requestRedraw();
  }
}

void GreeterSurface::commitImmediateFrame(bool layout) {
  m_deferredLayoutRequest = false;
  m_deferredRedrawRequest = false;
  if (m_window == nullptr) {
    return;
  }
  if (layout) {
    m_window->requestLayout();
  } else {
    m_window->requestRedraw();
  }
}

void GreeterSurface::prepareFrame(std::uint32_t width, std::uint32_t height, bool needsLayout) {
  if (!m_renderContext || m_window == nullptr) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!m_animTickInitialized) {
    m_lastAnimTick = now;
    m_animTickInitialized = true;
  } else {
    const float deltaMs = std::chrono::duration<float, std::milli>(now - m_lastAnimTick).count();
    m_lastAnimTick = now;
    if (deltaMs > 0.0f) {
      m_animations.tick(deltaMs);
    }
  }

  if (needsLayout) {
    m_renderContext->syncContentScale(m_window->renderTarget());
    syncWallpaperTexture();
    m_inLayout = true;
    layoutScene(width, height);
    m_inLayout = false;
  }
}

void GreeterSurface::syncScaledTypography() {
  if (m_headerUserGlyph != nullptr) {
    m_headerUserGlyph->setGlyphSize(Style::scaled(kHeaderUserIconBase));
  }
  m_formSubtitleLabel->setFontSize(Style::fontSizeTitle());
  m_brandTitleLabel->setFontSize(Style::scaled(30.0f));
  m_brandSubtitleLabel->setFontSize(Style::fontSizeCaption());
  m_passwordField->setControlHeight(Style::controlHeight());
  m_userSelectLabel->setFontSize(Style::fontSizeBody());
  m_userSelectGlyph->setGlyphSize(Style::fontSizeBody());
  m_sessionSelectLabel->setFontSize(Style::fontSizeBody());
  m_sessionSelectGlyph->setGlyphSize(Style::fontSizeBody());
  m_schemeSelectLabel->setFontSize(Style::fontSizeBody());
  m_schemeSelectGlyph->setGlyphSize(Style::fontSizeBody());
  m_loginButton->setGlyphSize(Style::fontSizeTitle());
  m_backButton->setGlyphSize(Style::fontSizeTitle());
  m_statusLabel->setFontSize(Style::fontSizeCaption());
}

void GreeterSurface::enterPasswordStep(std::size_t userIndex) {
  if (userIndex >= m_users.size()) {
    return;
  }
  m_selectedUser = userIndex;
  setUsername(m_users[userIndex]);
  m_userMenuOpen = false;
  m_userMenuSearchQuery.clear();
  m_passwordVisible = true;
  m_passwordField->setValue("");
  m_password.clear();
  if (m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
  }
  refreshSelectionLabels();
  notifyStateChanged();
  commitImmediateFrame(true);
}

void GreeterSurface::layoutScene(std::uint32_t width, std::uint32_t height) {
  auto* renderer = m_renderContext;
  if (!renderer)
    return;

  syncScaledTypography();

  const float fullW = static_cast<float>(width);
  const float fullH = static_cast<float>(height);
  const float ox = m_outputViewportActive ? m_viewportX : 0.0f;
  const float oy = m_outputViewportActive ? m_viewportY : 0.0f;
  const float sw = m_outputViewportActive ? m_viewportWidth : fullW;
  const float sh = m_outputViewportActive ? m_viewportHeight : fullH;

  m_root.setSize(fullW, fullH);

  if (m_letterbox != nullptr) {
    m_letterbox->setVisible(m_outputViewportActive);
    if (m_outputViewportActive) {
      m_letterbox->setPosition(0.0f, 0.0f);
      m_letterbox->setSize(fullW, fullH);
    }
  }

  if (m_wallpaper != nullptr) {
    m_wallpaper->setPosition(ox, oy);
    m_wallpaper->setSize(sw, sh);
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  }

  m_backdrop->setPosition(ox, oy);
  m_backdrop->setSize(sw, sh);
  if (m_hasSyncedWallpaper) {
    // WallpaperNode draws the image and any letterbox fill; keep backdrop
    // hidden so we do not paint wallpaperFillColor as a full-screen overlay on
    // top.
    m_backdrop->setVisible(false);
  } else {
    static_cast<RectNode*>(m_backdrop)
        ->setStyle(
            RoundedRectStyle{
                .fill = colorForRole(ColorRole::Surface),
                .fillMode = FillMode::Solid,
            }
        );
    m_backdrop->setVisible(true);
  }

  if (m_bottomBrandLogo != nullptr) {
    const bool hasBrandLogo = m_brandLogoTexture.id != 0 && !m_hideLogo;
    m_bottomBrandLogo->setVisible(hasBrandLogo);
    if (hasBrandLogo) {
      const float logoSize = Style::scaled(64.0f);
      m_bottomBrandLogo->setSize(logoSize, logoSize);
      m_bottomBrandLogo->setPosition(ox + std::round((sw - logoSize) * 0.5f), oy + sh - logoSize - Style::spaceLg());
      m_bottomBrandLogo->setTint(colorForRole(ColorRole::OnSurface, 0.88f));
    }
  }

  const float panelWidth = std::clamp(sw * 0.32f, Style::scaled(440.0f), Style::scaled(540.0f));
  const float rowHeight = Style::controlHeightLg();
  const float rowGap = Style::spaceSm();
  const float panelPadding = Style::spaceLg();

  const float userCount = static_cast<float>(m_users.size());
  const float glyphScale = std::clamp(1.0f - (userCount - 2.0f) * 0.08f, 0.75f, 1.0f);
  const float headerGlyphSize = Style::scaled(kHeaderUserIconBase) * glyphScale;

  if (m_passwordVisible && !m_users.empty() && m_selectedUser < m_users.size()) {
    m_formSubtitleLabel->setText(m_users[m_selectedUser]);
    m_formSubtitleLabel->setVisible(true);
  } else {
    m_formSubtitleLabel->setVisible(false);
  }
  if (m_formSubtitleLabel->visible()) {
    m_formSubtitleLabel->measure(*renderer);
  }
  m_statusLabel->measure(*renderer);

  const float subtitleGap = m_formSubtitleLabel->visible() ? (6.0f + m_formSubtitleLabel->height()) : 0.0f;
  const float headerTopPadding = Style::spaceSm();
  const float headerGlyphBlock = headerGlyphSize + Style::spaceMd();
  const float headerToContentGap = Style::spaceLg();
  const float headerBlockHeight = headerTopPadding + headerGlyphBlock + subtitleGap + headerToContentGap;

  const float sessionRowH = rowHeight;
  const float sessionRowGap = rowGap;

  const float userBlockHeight = showsUserDropdown() ? rowHeight : 0.0f;
  const float userPickerContentHeight = userBlockHeight + sessionRowGap + sessionRowH;
  const float passwordContentHeight = Style::controlHeight() + sessionRowGap + sessionRowH;
  const float contentBlockHeight = m_passwordVisible ? passwordContentHeight : userPickerContentHeight;

  const bool hasStatus = !m_status.empty();
  const std::string actualStatus = m_status;
  m_statusLabel->setText("Xy");
  m_statusLabel->measure(*renderer);
  const float statusLineHeight = m_statusLabel->height();
  m_statusLabel->setText(actualStatus);
  m_statusLabel->measure(*renderer);

  const float statusGap = Style::spaceSm();
  const float statusBlockHeight = hasStatus ? (statusGap + statusLineHeight) : 0.0f;
  const float panelTopPadding = panelPadding;
  const float panelBottomPadding = panelPadding;
  const float panelInnerHeight = headerBlockHeight + contentBlockHeight + statusBlockHeight;
  const float minPanelHeight = 0.0f;
  const float maxPanelHeight = std::max(minPanelHeight, sh - panelPadding * 2.0f);
  const float panelHeight =
      std::clamp(panelInnerHeight + panelTopPadding + panelBottomPadding, minPanelHeight, maxPanelHeight);
  const float panelX = ox + std::round((sw - panelWidth) * 0.5f);
  const float panelY = oy + std::round((sh - panelHeight) * 0.5f);
  const float contentLeft = panelX + panelPadding;
  const float contentWidth = panelWidth - panelPadding * 2.0f;

  float headerY = panelY + panelTopPadding + headerTopPadding;

  if (m_brandPane != nullptr) {
    static_cast<RectNode*>(m_brandPane)->setVisible(false);
  }

  // Refresh text colors each layout for theme/scheme changes.
  m_formSubtitleLabel->setColor(colorForRole(ColorRole::OnSurface));
  if (m_brandTitleLabel != nullptr) {
    m_brandTitleLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_brandSubtitleLabel != nullptr) {
    m_brandSubtitleLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_statusLabel != nullptr) {
    m_statusLabel->setColor(
        m_statusIsError ? colorForRole(ColorRole::Error) : colorForRole(ColorRole::OnSurfaceVariant)
    );
  }

  syncHeaderUserAvatar(*renderer, headerGlyphSize, panelX, panelWidth, headerY);
  headerY += headerGlyphBlock;

  if (m_formSubtitleLabel->visible()) {
    m_formSubtitleLabel->setPosition(
        (std::round(panelX + (panelWidth - m_formSubtitleLabel->width()) * 0.5f)), (headerY)
    );
    headerY += subtitleGap;
  }
  const float contentTop = panelY + panelTopPadding + headerBlockHeight;

  if (m_panelDivider != nullptr)
    m_panelDivider->setVisible(false);

  m_loginPanel->setPosition((panelX), (panelY));
  m_loginPanel->setSize(panelWidth, panelHeight);
  m_loginPanel->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::SurfaceVariant),
          .border = colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusXl(),
          .softness = 1.0f,
          .borderWidth = Style::borderWidth(),
      }
  );

  const float buttonWidth = Style::controlHeight();
  const float gap = Style::spaceSm();
  const float inputWidth = std::max(120.0f, contentWidth - buttonWidth - gap);

  const float selectorH = Style::controlHeightSm();
  const float schemeW = measureIconSelectorWidth(m_schemeSelectIcon, m_schemeSelectGlyph);
  const float schemeX = ox + sw - schemeW - Style::spaceLg();
  const float schemeY = oy + Style::spaceLg();
  layoutSelector(
      m_schemeSelectBox, m_schemeSelectIcon, m_schemeSelectGlyph, m_schemeSelectArea, schemeX, schemeY, schemeW,
      selectorH
  );
  if (m_schemeSelectLabel != nullptr) {
    m_schemeSelectLabel->setVisible(false);
  }

  layoutPowerButtons(ox, oy, sw, sh);

  if (!m_passwordVisible && showsUserDropdown()) {
    layoutPanelUserSelector(contentLeft, contentTop, contentWidth, rowHeight);
  } else {
    m_userSelectBox->setVisible(false);
    if (m_userSelectIcon != nullptr) {
      m_userSelectIcon->setVisible(false);
    }
    m_userSelectLabel->setVisible(false);
    m_userSelectGlyph->setVisible(false);
    m_userSelectArea->setVisible(false);
    if (m_userMenuSearchField != nullptr) {
      m_userMenuSearchField->setVisible(false);
    }
  }

  const bool showPasswordStep = m_passwordVisible;
  const bool showBackButton = showPasswordStep && showsUserDropdown();
  m_passwordField->setVisible(showPasswordStep);
  m_loginButton->setVisible(showPasswordStep);
  m_backButton->setVisible(showBackButton);
  if (showPasswordStep) {
    if (showBackButton) {
      const float backSize = Style::controlHeight();
      m_backButton->setSize(backSize, backSize);
      m_backButton->setPosition((contentLeft), (panelY + panelPadding));
      m_backButton->layout(*renderer);
      if (Glyph* backGlyph = m_backButton->glyph()) {
        (void)backGlyph->measure(*renderer);
        const auto glyphMetrics = renderer->measureGlyph(backGlyph->codepoint(), backGlyph->fontSize());
        const float glyphW = glyphMetrics.right - glyphMetrics.left;
        const float glyphH = glyphMetrics.bottom - glyphMetrics.top;
        const float glyphY = std::round(backSize * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
        backGlyph->setPosition(std::round(backSize * 0.5f - (glyphMetrics.left + glyphMetrics.right) * 0.5f), glyphY);
        backGlyph->setSize(std::max(glyphW, 1.0f), std::max(glyphH, 1.0f));
      }
    }

    m_passwordField->setSize(inputWidth, 0.0f);
    m_passwordField->setPosition((contentLeft), (contentTop));
    m_passwordField->layout(*renderer);

    m_loginButton->setSize(buttonWidth, Style::controlHeight());
    m_loginButton->setPosition((contentLeft + inputWidth + gap), (contentTop));
    m_loginButton->layout(*renderer);
    if (Glyph* loginGlyph = m_loginButton->glyph()) {
      (void)loginGlyph->measure(*renderer);
      const auto glyphMetrics = renderer->measureGlyph(loginGlyph->codepoint(), loginGlyph->fontSize());
      const float glyphW = glyphMetrics.right - glyphMetrics.left;
      const float glyphH = glyphMetrics.bottom - glyphMetrics.top;
      const float glyphY = std::round(Style::controlHeight() * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
      loginGlyph->setPosition(std::round(buttonWidth * 0.5f - (glyphMetrics.left + glyphMetrics.right) * 0.5f), glyphY);
      loginGlyph->setSize(std::max(glyphW, 1.0f), std::max(glyphH, 1.0f));
    }
  } else {
    m_backButton->setVisible(false);
  }

  const float sessionY = m_passwordVisible ? (contentTop + Style::controlHeight() + sessionRowGap)
                                           : (contentTop + userBlockHeight + sessionRowGap);
  layoutPanelSessionSelector(contentLeft, sessionY, contentWidth, sessionRowH);

  if (hasStatus) {
    const float statusY = contentTop + contentBlockHeight + statusGap;
    m_statusLabel->setVisible(true);
    m_statusLabel->setPosition((contentLeft), (statusY));
  } else {
    m_statusLabel->setVisible(false);
  }

  rebuildUserMenu();
  rebuildSessionMenu();
  rebuildSchemeMenu();

  rebuildFocusRing();
  applyMenuHighlight();

  if (!m_isKeyboardOwner) {
    return;
  }

  // Keyboard focus is applied once interactive targets have valid layout.
  const bool applyingInitialFocus = !m_initialFocusDone && !m_focusRing.empty();
  if (applyingInitialFocus) {
    setFocusIndex(defaultFocusIndex());
    m_initialFocusDone = true;
  } else if (m_focusIndex < 0 && !m_focusRing.empty()) {
    setFocusIndex(defaultFocusIndex());
  } else {
    syncFocusIndexFromFocused();
  }

  if (applyingInitialFocus
      && m_passwordVisible
      && m_passwordField != nullptr
      && m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
    syncFocusIndexFromFocused();
  }

  if (m_userMenuSearchFocusPending
      && m_userMenuSearchField != nullptr
      && m_userMenuSearchField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_userMenuSearchField->inputArea());
    m_userMenuSearchFocusPending = false;
  }
}

void GreeterSurface::tryAuthenticate() {
  // Ignore re-submits while a request is already in flight.
  if (m_greetdClient == nullptr || awaitingReply()) {
    return;
  }
  if (m_username.empty()) {
    updateStatus("Enter a username", true);
    commitImmediateFrame(false);
    return;
  }
  if (m_password.empty() && !m_allowEmptyPassword) {
    updateStatus("Enter a password", true);
    commitImmediateFrame(false);
    return;
  }

  if (!m_authSessionStarted) {
    // Arm the typed input (possibly empty) to answer the first secret prompt.
    m_pendingResponse = m_password;
    m_hasPendingResponse = true;
    m_authenticating = true;
    kLog.info("greetd: create_session for '{}'", m_username);
    if (!m_greetdClient->requestCreateSession(m_username)) {
      onAuthError(m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"}));
      return;
    }
    m_authSessionStarted = true;
    m_pendingReplies.push_back(AuthRequest::CreateSession);
    syncAuthInteractivity();
    commitImmediateFrame(false);
  } else if (m_secretPromptWaiting) {
    postAuthResponse(m_password);
  }
}

void GreeterSurface::onGreetdReadable() {
  if (m_greetdClient == nullptr) {
    return;
  }
  // Drain every frame currently buffered; readMessage() stops at nullopt.
  for (;;) {
    const std::optional<GreetdResponse> response = m_greetdClient->readMessage();
    if (!response.has_value()) {
      if (const auto error = m_greetdClient->lastError()) {
        kLog.warn("greetd connection lost: {}", error->description);
        // Drop the dead fd so it leaves the poll set instead of spinning on POLLHUP.
        m_greetdClient->disconnect();
        onAuthError(*error);
      }
      return;
    }
    handleGreetdResponse(*response);
  }
}

void GreeterSurface::handleGreetdResponse(const GreetdResponse& response) {
  if (m_pendingReplies.empty()) {
    return; // unsolicited reply — nothing to correlate it with
  }
  const AuthRequest expected = m_pendingReplies.front();
  m_pendingReplies.pop_front();

  const char* reqName = expected == AuthRequest::CreateSession ? "create_session"
      : expected == AuthRequest::PostAuthData                  ? "post_auth_data"
      : expected == AuthRequest::StartSession                  ? "start_session"
                                                               : "cancel_session";
  const char* respName = response.type == GreetdResponseType::AuthMessage ? "auth_message"
      : response.type == GreetdResponseType::Error                        ? "error"
                                                                          : "success";
  kLog.debug("greetd reply to {}: {}", reqName, respName);

  // Cancel ack drained the queue; re-enable input it had locked.
  if (expected == AuthRequest::Cancel) {
    syncAuthInteractivity();
    commitImmediateFrame(false);
    return;
  }

  switch (response.type) {
  case GreetdResponseType::Error:
    onAuthError(response.error);
    return;
  case GreetdResponseType::AuthMessage:
    handleAuthMessage(response.authMessage);
    return;
  case GreetdResponseType::Success:
    if (expected == AuthRequest::StartSession) {
      kLog.info("session start confirmed, exiting greeter");
      if (m_onExitRequested) {
        m_onExitRequested();
      }
    } else {
      // Auth finished; launch the session.
      beginSessionStart();
    }
    return;
  }
}

void GreeterSurface::handleAuthMessage(const GreetdAuthMessage& message) {
  const char* typeName = message.type == GreetdAuthMessageType::Secret ? "secret"
      : message.type == GreetdAuthMessageType::Visible                 ? "visible"
      : message.type == GreetdAuthMessageType::Error                   ? "error"
                                                                       : "info";
  kLog.info("PAM {} message: {}", typeName, message.message);

  if (message.type == GreetdAuthMessageType::Info || message.type == GreetdAuthMessageType::Error) {
    const bool isError = message.type == GreetdAuthMessageType::Error;
    if (!message.message.empty()) {
      updateStatus(message.message, isError);
    }
    if (isError) {
      clearPasswordInput();
    }
    // Ack with an empty response to resume PAM; paint now so the message shows.
    if (!m_greetdClient->requestPostAuthData("")) {
      onAuthError(m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"}));
      return;
    }
    m_pendingReplies.push_back(AuthRequest::PostAuthData);
    syncAuthInteractivity();
    commitImmediateFrame(false);
    return;
  }

  // Secret / Visible prompt. Answer with already-submitted input if we have it
  // (empty is allowed), otherwise surface the prompt and wait for the user.
  if (m_hasPendingResponse) {
    const std::string response = m_pendingResponse;
    m_hasPendingResponse = false;
    m_pendingResponse.clear();
    postAuthResponse(response);
    return;
  }

  m_secretPromptWaiting = true;
  if (!message.message.empty()) {
    updateStatus(message.message, false);
  }
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::postAuthResponse(const std::string& data) {
  kLog.debug("greetd: post_auth_data ({} bytes)", data.size());
  if (!m_greetdClient->requestPostAuthData(data)) {
    onAuthError(m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"}));
    return;
  }
  clearPasswordInput();
  m_secretPromptWaiting = false;
  m_pendingReplies.push_back(AuthRequest::PostAuthData);
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::syncAuthInteractivity() {
  const bool busy = awaitingReply();
  if (m_passwordField != nullptr) {
    m_passwordField->setEnabled(!busy);
  }
  if (m_loginButton != nullptr) {
    m_loginButton->setEnabled(!busy);
  }
}

void GreeterSurface::beginSessionStart() {
  kLog.info("authentication successful");

  savePreferences();

  if (m_greetdClient == nullptr) {
    kLog.error("no greetd client to start session");
    return;
  }

  GreetdSessionCommand cmd;
  if (!m_sessions.empty() && m_selectedSession < m_sessions.size()) {
    cmd.command = m_sessions[m_selectedSession].command;
  } else {
    cmd.command = "/bin/sh";
  }

  if (!m_greetdClient->requestStartSession(cmd)) {
    m_authenticating = false;
    resetAuthSession();
    clearPasswordInput();
    syncAuthInteractivity();
    if (m_greetdClient->lastError()) {
      kLog.error("start_session failed: {}", m_greetdClient->lastError()->description);
      updateStatus("Failed to start session: " + m_greetdClient->lastError()->description, true);
    }
    commitImmediateFrame(false);
    return;
  }

  m_pendingReplies.push_back(AuthRequest::StartSession);
}

void GreeterSurface::resetAuthSession() {
  if (m_greetdClient != nullptr && m_authSessionStarted) {
    if (m_greetdClient->requestCancelSession()) {
      // Track the ack so a later create_session reply is not mistaken for it.
      m_pendingReplies.push_back(AuthRequest::Cancel);
    }
  }
  m_authSessionStarted = false;
}

void GreeterSurface::clearPasswordInput() {
  m_password.clear();
  if (m_passwordField != nullptr) {
    m_passwordField->setValue("");
  }
}

void GreeterSurface::onAuthError(const GreetdError& error) {
  m_authenticating = false;
  m_secretPromptWaiting = false;
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  m_pendingReplies.clear();
  clearPasswordInput();
  resetAuthSession();
  syncAuthInteractivity();
  updateStatus(error.description, true);
  kLog.warn("authentication failed: {}", error.description);
  commitImmediateFrame(false);
}

void GreeterSurface::updateStatus(const std::string& text, bool isError) {
  // Empty text clears the line; non-empty text shows as an error or, for PAM
  // info prompts, as a neutral hint rather than being discarded.
  m_status = text;
  m_statusIsError = isError;
  if (m_statusLabel != nullptr) {
    m_statusLabel->setText(text);
    m_statusLabel->setColor(colorForRole(isError ? ColorRole::Error : ColorRole::OnSurfaceVariant));
  }
  requestLayout();
}

void GreeterSurface::loadUsers() {
  m_users.clear();
  m_userUids.clear();
  m_userIconPaths.clear();
  static const std::unordered_set<std::string> kHiddenSystemUsers = {
      "greeter", "greetd", "sddm", "lightdm", "gdm", "nobody",
  };

  int userEnumerationErrno = 0;

  ::setpwent();
  while (true) {
    errno = 0;
    struct passwd* pw = ::getpwent();
    if (pw == nullptr) {
      userEnumerationErrno = errno;
      break;
    }

    if (pw->pw_name == nullptr || pw->pw_shell == nullptr) {
      continue;
    }

    std::string user = pw->pw_name;
    std::string shell = pw->pw_shell;
    uid_t uid = pw->pw_uid;

    if (uid < 1000 || kHiddenSystemUsers.contains(user)) {
      continue;
    }
    if (shell.find("nologin") != std::string::npos || shell.find("false") != std::string::npos) {
      continue;
    }
    m_users.push_back(user);
    m_userUids.push_back(uid);
  }
  ::endpwent();

  if (userEnumerationErrno != 0) {
    kLog.warn("failed to enumerate NSS users: {}", std::strerror(userEnumerationErrno));
  }

  if (m_users.empty()) {
    m_users.push_back("greeter");
    m_userUids.push_back(0);
  }

  appendDummyUsers(m_users, m_userUids);

  m_userIconPaths.reserve(m_userUids.size());
  for (const uid_t uid : m_userUids) {
    m_userIconPaths.push_back(accounts::iconFileForUid(uid).value_or(""));
  }

  m_selectedUser = 0;
  setUsername(m_users[m_selectedUser]);
}

void GreeterSurface::loadSessions() {
  m_sessions = greeter::discoverSessions();
  m_selectedSession = 0;
}

void GreeterSurface::refreshSelectionLabels() {
  if (m_userSelectLabel != nullptr) {
    const std::string userLabel = m_users.empty() ? "(none)" : m_users[std::min(m_selectedUser, m_users.size() - 1)];
    m_userSelectLabel->setText(userLabel);
    m_userSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_sessionSelectLabel != nullptr) {
    const std::string sessionLabel =
        m_sessions.empty() ? "/bin/sh" : m_sessions[std::min(m_selectedSession, m_sessions.size() - 1)].name;
    m_sessionSelectLabel->setText(sessionLabel);
  }
  if (m_userSelectGlyph != nullptr) {
    m_userSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  syncPanelUserChrome();
  syncPanelSessionChrome();
  if (m_schemeSelectLabel != nullptr) {
    const std::string schemeLabel =
        m_schemeNames.empty() ? "Noctalia" : m_schemeNames[std::min(m_selectedScheme, m_schemeNames.size() - 1)];
    m_schemeSelectLabel->setText(schemeLabel);
    m_schemeSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_schemeSelectGlyph != nullptr) {
    m_schemeSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_schemeSelectIcon != nullptr) {
    m_schemeSelectIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  applySelectorBoxStyle(m_schemeSelectBox, m_schemeSelectArea);
}

void GreeterSurface::buildSchemeNames() {
  m_schemeNames.clear();
  m_syncedAppearance = loadGreeterSyncedAppearance();
  if (m_syncedAppearance.has_value()) {
    m_schemeNames.emplace_back(greeter::appearance::kSyncedSchemeDisplayName);
    m_selectedScheme = 0;
  }

  for (const auto& builtinPalette : noctalia::theme::builtinPalettes()) {
    m_schemeNames.emplace_back(builtinPalette.name);
    if (!m_syncedAppearance.has_value() && builtinPalette.name == "Noctalia") {
      m_selectedScheme = m_schemeNames.size() - 1;
    }
  }
}

bool GreeterSurface::isSyncedScheme(const std::size_t schemeIndex) const {
  return schemeIndex < m_schemeNames.size()
      && m_schemeNames[schemeIndex] == greeter::appearance::kSyncedSchemeDisplayName;
}

std::optional<std::size_t> GreeterSurface::findSchemeIndex(const std::string_view name) const {
  for (std::size_t i = 0; i < m_schemeNames.size(); ++i) {
    if (m_schemeNames[i] == name) {
      return i;
    }
  }
  return std::nullopt;
}

void GreeterSurface::clearWallpaperDisplay() {
  m_hasSyncedWallpaper = false;
  m_wallpaperPath.clear();
  m_wallpaperFillMode = WallpaperFillMode::Crop;
  m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  m_wallpaperDirty = true;
}

void GreeterSurface::applyScheme(const std::size_t schemeIndex) {
  if (schemeIndex >= m_schemeNames.size()) {
    return;
  }

  m_selectedScheme = schemeIndex;
  if (isSyncedScheme(schemeIndex)) {
    if (!m_syncedAppearance.has_value()) {
      m_syncedAppearance = loadGreeterSyncedAppearance();
    }
    if (!m_syncedAppearance.has_value()) {
      if (const auto fallback = findSchemeIndex("Noctalia")) {
        applyScheme(*fallback);
      }
      return;
    }

    setPalette(m_syncedAppearance->palette);
    Style::setCornerRadiusScale(m_syncedAppearance->cornerRadiusScale);
    m_wallpaperPath = m_syncedAppearance->wallpaperPath;
    m_wallpaperFillMode = m_syncedAppearance->wallpaperFillMode;
    m_wallpaperFillColor = m_syncedAppearance->wallpaperFillColor;
    m_hasSyncedWallpaper = !m_wallpaperPath.empty();
    m_wallpaperDirty = true;
    return;
  }

  if (const auto* builtinPalette = noctalia::theme::findBuiltinPalette(m_schemeNames[schemeIndex])) {
    setPalette(builtinPalette->dark.palette);
  }
  Style::setCornerRadiusScale(1.0f);
  clearWallpaperDisplay();
}

void GreeterSurface::syncHeaderUserAvatar(
    Renderer& renderer, const float size, const float panelX, const float panelWidth, const float headerY
) {
  const bool canShowAvatar = !m_users.empty()
      && m_selectedUser < m_users.size()
      && m_selectedUser < m_userIconPaths.size()
      && m_headerUserAvatar != nullptr
      && m_renderContext != nullptr;
  const std::string iconPath = canShowAvatar ? m_userIconPaths[m_selectedUser] : std::string{};

  if (canShowAvatar && !iconPath.empty() && iconPath != m_loadedHeaderAvatarPath) {
    if (m_headerAvatarTexture.id != 0) {
      m_renderContext->textureManager().unload(m_headerAvatarTexture);
      m_headerAvatarTexture = {};
    }
    m_loadedHeaderAvatarPath = iconPath;
    m_headerAvatarTexture =
        m_renderContext->textureManager().loadFromFile(iconPath, static_cast<int>(std::lround(size)), true);
  }

  if (!canShowAvatar || iconPath.empty() || m_headerAvatarTexture.id == 0) {
    if (m_headerUserAvatar != nullptr) {
      m_headerUserAvatar->setVisible(false);
    }
    if (m_headerUserGlyph != nullptr) {
      m_headerUserGlyph->setVisible(true);
      m_headerUserGlyph->setGlyphSize(size);
      m_headerUserGlyph->setColor(colorForRole(ColorRole::OnSurface));
      (void)m_headerUserGlyph->measure(renderer);
      const auto glyphMetrics = renderer.measureGlyph(m_headerUserGlyph->codepoint(), m_headerUserGlyph->fontSize());
      const float glyphY = headerY + std::round(size * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
      m_headerUserGlyph->setPosition(
          std::round(panelX + panelWidth * 0.5f - (glyphMetrics.left + glyphMetrics.right) * 0.5f), glyphY
      );
    }
    return;
  }

  if (m_headerUserGlyph != nullptr) {
    m_headerUserGlyph->setVisible(false);
  }

  const float avatarX = std::round(panelX + panelWidth * 0.5f - size * 0.5f);
  const float avatarY = headerY;
  m_headerUserAvatar->setTextureId(m_headerAvatarTexture.id);
  m_headerUserAvatar->setTextureSize(m_headerAvatarTexture.width, m_headerAvatarTexture.height);
  m_headerUserAvatar->setTint({1.0f, 1.0f, 1.0f, 1.0f});
  m_headerUserAvatar->setRadius(size * 0.5f);
  m_headerUserAvatar->setBorder(colorForRole(ColorRole::Primary), Style::borderWidth() * kHeaderAvatarBorderScale);
  m_headerUserAvatar->setFitMode(ImageFitMode::Cover);
  m_headerUserAvatar->setSize(size, size);
  m_headerUserAvatar->setPosition(avatarX, avatarY);
  m_headerUserAvatar->setVisible(true);
}

void GreeterSurface::syncWallpaperTexture() {
  if (!m_wallpaperDirty || m_wallpaper == nullptr || m_renderContext == nullptr) {
    return;
  }

  if (m_wallpaperTexture.id != 0) {
    m_renderContext->textureManager().unload(m_wallpaperTexture);
    m_wallpaperTexture = {};
  }

  Color color;
  if (parseColorWallpaperPath(m_wallpaperPath, color)) {
    m_wallpaper->setSources(
        WallpaperSourceKind::Color, {}, color, WallpaperSourceKind::Color, {}, color, 0.0f, 0.0f, 0.0f, 0.0f
    );
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  } else if (!m_wallpaperPath.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(m_wallpaperPath, ec) && !ec) {
      m_wallpaperTexture = m_renderContext->textureManager().loadFromFile(m_wallpaperPath, 0, true);
      if (m_wallpaperTexture.id != 0) {
        m_wallpaper->setTextures(
            m_wallpaperTexture.id, {}, static_cast<float>(m_wallpaperTexture.width),
            static_cast<float>(m_wallpaperTexture.height), 0.0f, 0.0f
        );
        m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
        m_wallpaper->setFillMode(m_wallpaperFillMode);
        m_wallpaper->setFillColor(m_wallpaperFillColor);
      } else {
        m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
      }
    } else {
      m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
    }
  } else {
    m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
  }

  m_wallpaperDirty = false;
}

void GreeterSurface::loadPreferences() {
  const auto prefs = greeter::loadGreeterPreferences();
  m_allowEmptyPassword = prefs.allowEmptyPassword;
  const auto initialSession = greeter::resolveInitialSessionName(prefs);

  if (initialSession.has_value()) {
    if (const auto index = greeter::findSessionIndex(m_sessions, *initialSession)) {
      m_selectedSession = *index;
    }
  }

  if (prefs.scheme.has_value()) {
    for (std::size_t i = 0; i < m_schemeNames.size(); ++i) {
      if (m_schemeNames[i] == *prefs.scheme) {
        m_selectedScheme = i;
        break;
      }
    }
  }

  m_hideLogo = prefs.hideLogo;
}

void GreeterSurface::savePreferences() const {
  greeter::GreeterPreferences prefs;
  if (m_selectedSession < m_sessions.size()) {
    prefs.session = m_sessions[m_selectedSession].name;
  }
  if (m_selectedScheme < m_schemeNames.size()) {
    prefs.scheme = m_schemeNames[m_selectedScheme];
  }

  if (!greeter::saveGreeterPreferences(prefs)) {
    kLog.warn("failed to save greeter.toml (check permissions on {})", greeter::greeterConfPath().string());
  }
}

void GreeterSurface::toggleUserMenu() {
  if (m_userMenuOpen) {
    m_userMenuOpen = false;
    m_userMenuSearchQuery.clear();
    if (m_userMenuSearchField != nullptr) {
      m_userMenuSearchField->setValue("");
    }
  } else {
    openUserMenu();
    return;
  }
  requestLayout();
}

void GreeterSurface::openUserMenu() {
  if (m_userMenuOpen || m_passwordVisible || !showsUserDropdown()) {
    return;
  }
  m_userMenuOpen = true;
  m_sessionMenuOpen = false;
  clearSessionMenu();
  m_userMenuSearchQuery.clear();
  if (m_userMenuSearchField != nullptr) {
    m_userMenuSearchField->setValue("");
  }
  m_userMenuScrollOffset = 0;
  m_userMenuSearchFocusPending = true;
  requestLayout();
}

void GreeterSurface::toggleSessionMenu() {
  m_sessionMenuOpen = !m_sessionMenuOpen;
  if (m_sessionMenuOpen) {
    m_userMenuOpen = false;
    m_schemeMenuOpen = false;
    clearUserMenu();
    clearSchemeMenu();
  }
  m_menuHighlight = -1;
  requestLayout();
}

void GreeterSurface::toggleSchemeMenu() {
  m_schemeMenuOpen = !m_schemeMenuOpen;
  if (m_schemeMenuOpen) {
    m_userMenuOpen = false;
    m_sessionMenuOpen = false;
    clearUserMenu();
    clearSessionMenu();
  }
  m_menuHighlight = -1;
  requestLayout();
}

void GreeterSurface::closeMenus() {
  m_userMenuOpen = false;
  m_sessionMenuOpen = false;
  m_schemeMenuOpen = false;
  m_userMenuSearchQuery.clear();
  m_menuHighlight = -1;
  clearUserMenu();
  clearSessionMenu();
  clearSchemeMenu();
}

void GreeterSurface::closeMenusAndRestoreFocus() {
  InputArea* owner = m_userMenuOpen ? m_userSelectArea
      : m_sessionMenuOpen           ? m_sessionSelectArea
      : m_schemeMenuOpen            ? m_schemeSelectArea
                                    : nullptr;
  closeMenus();
  if (owner != nullptr) {
    m_inputDispatcher.setFocus(owner);
  }
  requestLayout();
}

void GreeterSurface::selectSession(std::size_t index) {
  if (index >= m_sessions.size()) {
    return;
  }
  m_selectedSession = index;
  refreshSelectionLabels();
  savePreferences();
  m_sessionMenuOpen = false;
  m_menuHighlight = -1;
  if (m_sessionSelectArea != nullptr) {
    m_inputDispatcher.setFocus(m_sessionSelectArea);
  }
  notifyStateChanged();
  commitImmediateFrame(true);
}

void GreeterSurface::selectScheme(std::size_t index) {
  if (index >= m_schemeNames.size()) {
    return;
  }
  applyScheme(index);
  refreshSelectionLabels();
  savePreferences();
  m_schemeMenuOpen = false;
  m_menuHighlight = -1;
  if (m_schemeSelectArea != nullptr) {
    m_inputDispatcher.setFocus(m_schemeSelectArea);
  }
  notifyStateChanged();
  commitImmediateFrame(true);
}

void GreeterSurface::runBackAction() {
  if (!showsUserDropdown()) {
    return;
  }
  m_passwordVisible = false;
  m_passwordField->setValue("");
  m_password.clear();
  updateStatus("", false);
  if (m_userSelectArea != nullptr) {
    m_inputDispatcher.setFocus(m_userSelectArea);
  }
  notifyStateChanged();
  commitImmediateFrame(true);
}

bool GreeterSurface::menuOpen() const noexcept { return m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen; }

void GreeterSurface::rebuildFocusRing() {
  InputArea* previouslyFocused = InputArea::getFocused();
  m_focusRing.clear();

  if (m_passwordVisible) {
    if (m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
      m_focusRing.push_back({m_passwordField->inputArea(), {}});
    }
    if (m_loginButton != nullptr && m_loginButton->inputArea() != nullptr) {
      m_focusRing.push_back({m_loginButton->inputArea(), [this]() { tryAuthenticate(); }});
    }
    if (m_backButton != nullptr && m_backButton->inputArea() != nullptr && showsUserDropdown()) {
      m_focusRing.push_back({m_backButton->inputArea(), [this]() { runBackAction(); }});
    }
  } else if (showsUserDropdown() && m_userSelectArea != nullptr) {
    m_focusRing.push_back({m_userSelectArea, [this]() {
                             if (!m_passwordVisible && m_selectedUser < m_users.size()) {
                               enterPasswordStep(m_selectedUser);
                             }
                           }});
  }

  if (m_sessionSelectArea != nullptr) {
    m_focusRing.push_back({m_sessionSelectArea, [this]() { toggleSessionMenu(); }});
  }
  if (m_schemeSelectArea != nullptr) {
    m_focusRing.push_back({m_schemeSelectArea, [this]() { toggleSchemeMenu(); }});
  }

  if (m_firmwareButton != nullptr && m_firmwareButton->inputArea() != nullptr) {
    m_focusRing.push_back({m_firmwareButton->inputArea(), []() { power::rebootToFirmwareSetup(); }});
  }
  if (m_rebootButton != nullptr && m_rebootButton->inputArea() != nullptr) {
    m_focusRing.push_back({m_rebootButton->inputArea(), []() { power::reboot(); }});
  }
  if (m_shutdownButton != nullptr && m_shutdownButton->inputArea() != nullptr) {
    m_focusRing.push_back({m_shutdownButton->inputArea(), []() { power::powerOff(); }});
  }

  // Keep focus on whatever was focused before the rebuild, if still present.
  m_focusIndex = -1;
  for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
    if (m_focusRing[i].area == previouslyFocused) {
      m_focusIndex = static_cast<std::ptrdiff_t>(i);
      break;
    }
  }
}

float GreeterSurface::measureIconSelectorWidth(Glyph* icon, Glyph* chevron) const {
  if (m_renderContext == nullptr) {
    return Style::controlHeightSm();
  }

  auto& renderer = *m_renderContext;
  float width = Style::spaceMd() * 2.0f + Style::spaceSm();
  if (icon != nullptr) {
    (void)icon->measure(renderer);
    const auto iconMetrics = renderer.measureGlyph(icon->codepoint(), icon->fontSize());
    width += iconMetrics.right - iconMetrics.left;
  }
  if (chevron != nullptr) {
    width += Style::spaceSm();
    (void)chevron->measure(renderer);
    const auto chevronMetrics = renderer.measureGlyph(chevron->codepoint(), chevron->fontSize());
    width += chevronMetrics.right - chevronMetrics.left;
  }
  return std::max(width, Style::controlHeightSm());
}

void GreeterSurface::layoutSelector(
    Box* box, Glyph* icon, Glyph* chevron, InputArea* area, float x, float y, float w, float h
) {
  auto* renderer = m_renderContext;
  if (renderer == nullptr || box == nullptr || chevron == nullptr || area == nullptr) {
    return;
  }

  applySelectorBoxStyle(box, area);
  box->setVisible(true);
  box->setPosition(x, y);
  box->setSize(w, h);
  box->layout(*renderer);

  if (icon != nullptr) {
    icon->setVisible(true);
    icon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    (void)icon->measure(*renderer);
    const auto iconMetrics = renderer->measureGlyph(icon->codepoint(), icon->fontSize());
    const float iconY = y + std::round(h * 0.5f - (iconMetrics.top + iconMetrics.bottom) * 0.5f);
    icon->setPosition(x + Style::spaceMd(), iconY);
  }

  chevron->setVisible(true);
  chevron->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  (void)chevron->measure(*renderer);
  const auto glyphMetrics = renderer->measureGlyph(chevron->codepoint(), chevron->fontSize());
  const float glyphW = glyphMetrics.right - glyphMetrics.left;
  const float glyphY = y + std::round(h * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
  chevron->setPosition(x + w - Style::spaceMd() - glyphW, glyphY);

  area->setVisible(true);
  area->setPosition(x, y);
  area->setSize(w, h);
}

void GreeterSurface::applySelectorBoxStyle(Box* box, const InputArea* area) {
  if (box == nullptr) {
    return;
  }
  const bool menuOpen = (box == m_schemeSelectBox && m_schemeMenuOpen) || (box == m_userSelectBox && m_userMenuOpen);
  const bool active = menuOpen || (area != nullptr && area->focused());
  box->setStyle(
      RoundedRectStyle{
          .fill = active ? colorForRole(ColorRole::Surface) : colorForRole(ColorRole::SurfaceVariant),
          .border = active ? colorForRole(ColorRole::Primary) : colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = active ? std::max(Style::borderWidth(), Style::borderWidth() * 2.0f) : Style::borderWidth(),
      }
  );
}

void GreeterSurface::syncPanelUserChrome() {
  if (m_userSelectBox == nullptr || m_userSelectArea == nullptr) {
    return;
  }

  const InputArea* area = m_userSelectArea;
  const bool active = m_userMenuOpen || area->focused();

  m_userSelectBox->setStyle(
      RoundedRectStyle{
          .fill = active ? colorForRole(ColorRole::Secondary) : colorForRole(ColorRole::Surface),
          .border = active ? colorForRole(ColorRole::Primary) : colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = active ? std::max(Style::borderWidth(), Style::borderWidth() * 2.0f) : Style::borderWidth(),
      }
  );

  const Color labelColor = active ? colorForRole(ColorRole::OnSecondary) : colorForRole(ColorRole::OnSurface);
  const Color iconColor = active ? colorForRole(ColorRole::OnSecondary) : colorForRole(ColorRole::OnSurfaceVariant);
  if (m_userSelectLabel != nullptr) {
    m_userSelectLabel->setColor(labelColor);
  }
  if (m_userSelectIcon != nullptr) {
    m_userSelectIcon->setColor(iconColor);
  }
  if (m_userSelectGlyph != nullptr) {
    m_userSelectGlyph->setColor(iconColor);
  }
  if (m_userMenuSearchField != nullptr) {
    m_userMenuSearchField->setActiveChrome(m_userMenuOpen);
  }
}

void GreeterSurface::layoutPanelUserSelector(float x, float y, float w, float h) {
  auto* renderer = m_renderContext;
  if (renderer == nullptr
      || m_userSelectBox == nullptr
      || m_userSelectLabel == nullptr
      || m_userSelectGlyph == nullptr
      || m_userSelectArea == nullptr) {
    return;
  }

  syncPanelUserChrome();
  m_userSelectBox->setVisible(true);
  m_userSelectBox->setPosition(x, y);
  m_userSelectBox->setSize(w, h);
  m_userSelectBox->layout(*renderer);

  float textX = x + Style::spaceMd();
  float chevronReserve = Style::spaceMd();
  if (m_userSelectGlyph != nullptr) {
    (void)m_userSelectGlyph->measure(*renderer);
    const auto chevronMetrics = renderer->measureGlyph(m_userSelectGlyph->codepoint(), m_userSelectGlyph->fontSize());
    chevronReserve += chevronMetrics.right - chevronMetrics.left;
  }

  if (m_userSelectIcon != nullptr) {
    m_userSelectIcon->setVisible(true);
    (void)m_userSelectIcon->measure(*renderer);
    const auto iconMetrics = renderer->measureGlyph(m_userSelectIcon->codepoint(), m_userSelectIcon->fontSize());
    const float iconW = iconMetrics.right - iconMetrics.left;
    const float iconY = y + std::round(h * 0.5f - (iconMetrics.top + iconMetrics.bottom) * 0.5f);
    m_userSelectIcon->setPosition(textX, iconY);
    textX += iconW + Style::spaceSm();
  }

  const float fieldW = std::max(0.0f, w - (textX - x) - chevronReserve - Style::spaceSm());
  const float openFieldX = x + Style::spaceMd();
  const float openFieldW = std::max(0.0f, w - Style::spaceMd() - chevronReserve - Style::spaceSm());

  if (m_userMenuOpen && m_userMenuSearchField != nullptr) {
    if (m_userSelectIcon != nullptr) {
      m_userSelectIcon->setVisible(false);
    }
    m_userSelectLabel->setVisible(false);
    m_userSelectArea->setVisible(false);
    m_userMenuSearchField->setVisible(true);
    layoutUserMenuSearchField(openFieldX, y, openFieldW, h);
  } else {
    if (m_userMenuSearchField != nullptr) {
      m_userMenuSearchField->setVisible(false);
    }
    m_userSelectLabel->setVisible(true);
    m_userSelectLabel->setMaxWidth(fieldW);
    m_userSelectLabel->measure(*renderer);
    m_userSelectLabel->setPosition(textX, y + std::round((h - m_userSelectLabel->height()) * 0.5f));
    m_userSelectArea->setVisible(true);
    m_userSelectArea->setPosition(x, y);
    m_userSelectArea->setSize(w, h);
  }

  m_userSelectGlyph->setVisible(true);
  (void)m_userSelectGlyph->measure(*renderer);
  const auto glyphMetrics = renderer->measureGlyph(m_userSelectGlyph->codepoint(), m_userSelectGlyph->fontSize());
  const float glyphW = glyphMetrics.right - glyphMetrics.left;
  const float glyphY = y + std::round(h * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
  m_userSelectGlyph->setPosition(x + w - Style::spaceMd() - glyphW, glyphY);
}

void GreeterSurface::syncPanelSessionChrome() {
  if (m_sessionSelectBox == nullptr || m_sessionSelectArea == nullptr) {
    return;
  }

  const InputArea* area = m_sessionSelectArea;
  const bool active = m_sessionMenuOpen || area->focused();

  m_sessionSelectBox->setStyle(
      RoundedRectStyle{
          .fill = active ? colorForRole(ColorRole::Secondary) : colorForRole(ColorRole::Surface),
          .border = active ? colorForRole(ColorRole::Primary) : colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = active ? std::max(Style::borderWidth(), Style::borderWidth() * 2.0f) : Style::borderWidth(),
      }
  );

  const Color labelColor = active ? colorForRole(ColorRole::OnSecondary) : colorForRole(ColorRole::OnSurface);
  const Color iconColor = active ? colorForRole(ColorRole::OnSecondary) : colorForRole(ColorRole::OnSurfaceVariant);
  if (m_sessionSelectLabel != nullptr) {
    m_sessionSelectLabel->setColor(labelColor);
  }
  if (m_sessionSelectIcon != nullptr) {
    m_sessionSelectIcon->setColor(iconColor);
  }
  if (m_sessionSelectGlyph != nullptr) {
    m_sessionSelectGlyph->setColor(iconColor);
  }
}

void GreeterSurface::layoutPanelSessionSelector(float x, float y, float w, float h) {
  auto* renderer = m_renderContext;
  if (renderer == nullptr
      || m_sessionSelectBox == nullptr
      || m_sessionSelectLabel == nullptr
      || m_sessionSelectGlyph == nullptr
      || m_sessionSelectArea == nullptr) {
    return;
  }

  syncPanelSessionChrome();
  m_sessionSelectBox->setVisible(true);
  m_sessionSelectBox->setPosition(x, y);
  m_sessionSelectBox->setSize(w, h);
  m_sessionSelectBox->layout(*renderer);

  float textX = x + Style::spaceMd();
  float chevronReserve = Style::spaceMd();
  if (m_sessionSelectGlyph != nullptr) {
    (void)m_sessionSelectGlyph->measure(*renderer);
    const auto chevronMetrics =
        renderer->measureGlyph(m_sessionSelectGlyph->codepoint(), m_sessionSelectGlyph->fontSize());
    chevronReserve += chevronMetrics.right - chevronMetrics.left;
  }

  if (m_sessionSelectIcon != nullptr) {
    m_sessionSelectIcon->setVisible(true);
    (void)m_sessionSelectIcon->measure(*renderer);
    const auto iconMetrics = renderer->measureGlyph(m_sessionSelectIcon->codepoint(), m_sessionSelectIcon->fontSize());
    const float iconW = iconMetrics.right - iconMetrics.left;
    const float iconY = y + std::round(h * 0.5f - (iconMetrics.top + iconMetrics.bottom) * 0.5f);
    m_sessionSelectIcon->setPosition(textX, iconY);
    textX += iconW + Style::spaceSm();
  }

  m_sessionSelectLabel->setVisible(true);
  m_sessionSelectLabel->setMaxWidth(std::max(0.0f, w - (textX - x) - chevronReserve - Style::spaceSm()));
  m_sessionSelectLabel->measure(*renderer);
  m_sessionSelectLabel->setPosition(textX, y + std::round((h - m_sessionSelectLabel->height()) * 0.5f));

  m_sessionSelectGlyph->setVisible(true);
  (void)m_sessionSelectGlyph->measure(*renderer);
  const auto glyphMetrics = renderer->measureGlyph(m_sessionSelectGlyph->codepoint(), m_sessionSelectGlyph->fontSize());
  const float glyphW = glyphMetrics.right - glyphMetrics.left;
  const float glyphY = y + std::round(h * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
  m_sessionSelectGlyph->setPosition(x + w - Style::spaceMd() - glyphW, glyphY);

  m_sessionSelectArea->setVisible(true);
  m_sessionSelectArea->setPosition(x, y);
  m_sessionSelectArea->setSize(w, h);
}

std::ptrdiff_t GreeterSurface::defaultFocusIndex() const {
  if (m_focusRing.empty()) {
    return 0;
  }

  if (m_passwordVisible && m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
    for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
      if (m_focusRing[i].area == m_passwordField->inputArea()) {
        return static_cast<std::ptrdiff_t>(i);
      }
    }
  }

  if (!m_passwordVisible && showsUserDropdown() && m_userSelectArea != nullptr) {
    for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
      if (m_focusRing[i].area == m_userSelectArea) {
        return static_cast<std::ptrdiff_t>(i);
      }
    }
  }

  return 0;
}

void GreeterSurface::syncFocusIndexFromFocused() {
  InputArea* focused = InputArea::getFocused();
  if (focused == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
    if (m_focusRing[i].area == focused) {
      m_focusIndex = static_cast<std::ptrdiff_t>(i);
      return;
    }
  }
}

void GreeterSurface::layoutPowerButtons(float ox, float oy, float sw, float sh) {
  auto* renderer = m_renderContext;
  if (renderer == nullptr) {
    return;
  }

  const float size = Style::controlHeight();
  const float margin = Style::spaceLg();
  const float gap = Style::spaceSm();
  const float bottom = oy + sh - size - margin;

  const auto place = [&](Button* btn, float x) {
    if (btn == nullptr) {
      return;
    }
    btn->setVisible(true);
    btn->setRadius(size * 0.5f);
    btn->setSize(size, size);
    btn->setPosition(x, bottom);
    btn->layout(*renderer);
    if (Glyph* glyph = btn->glyph()) {
      (void)glyph->measure(*renderer);
      const auto metrics = renderer->measureGlyph(glyph->codepoint(), glyph->fontSize());
      const float glyphW = metrics.right - metrics.left;
      const float glyphH = metrics.bottom - metrics.top;
      glyph->setPosition(
          std::round(size * 0.5f - (metrics.left + metrics.right) * 0.5f),
          std::round(size * 0.5f - (metrics.top + metrics.bottom) * 0.5f)
      );
      glyph->setSize(std::max(glyphW, 1.0f), std::max(glyphH, 1.0f));
    }
  };

  float x = ox + sw - size - margin;
  place(m_shutdownButton, x);
  x -= size + gap;
  place(m_rebootButton, x);
  if (m_firmwareButton != nullptr) {
    x -= size + gap;
    place(m_firmwareButton, x);
  }
}

void GreeterSurface::setFocusIndex(std::ptrdiff_t index) {
  if (m_focusRing.empty()) {
    m_focusIndex = -1;
    return;
  }
  const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(m_focusRing.size());
  index = ((index % count) + count) % count;
  m_focusIndex = index;
  m_inputDispatcher.setFocus(m_focusRing[static_cast<std::size_t>(index)].area);
  requestRedraw();
}

void GreeterSurface::moveFocus(int delta) {
  if (m_focusRing.empty()) {
    return;
  }
  if (m_focusIndex < 0) {
    syncFocusIndexFromFocused();
  }
  const std::ptrdiff_t start = m_focusIndex < 0 ? 0 : m_focusIndex + delta;
  setFocusIndex(start);
}

void GreeterSurface::activateFocused() {
  if (m_focusRing.empty()) {
    return;
  }
  if (m_focusIndex < 0 || m_focusIndex >= static_cast<std::ptrdiff_t>(m_focusRing.size())) {
    syncFocusIndexFromFocused();
  }
  if (m_focusIndex < 0 || m_focusIndex >= static_cast<std::ptrdiff_t>(m_focusRing.size())) {
    setFocusIndex(defaultFocusIndex());
  }
  if (m_focusIndex < 0 || m_focusIndex >= static_cast<std::ptrdiff_t>(m_focusRing.size())) {
    return;
  }
  const auto& activate = m_focusRing[static_cast<std::size_t>(m_focusIndex)].activate;
  if (activate) {
    activate();
  }
}

void GreeterSurface::applyMenuHighlight() {
  std::vector<Box*>* rows = nullptr;
  std::vector<Label*>* labels = nullptr;
  std::size_t selected = 0;
  if (m_sessionMenuOpen) {
    rows = &m_sessionMenuRows;
    labels = &m_sessionMenuLabels;
    selected = m_selectedSession;
  } else if (m_schemeMenuOpen) {
    rows = &m_schemeMenuRows;
    labels = &m_schemeMenuLabels;
    selected = m_selectedScheme;
  } else if (m_userMenuOpen) {
    rows = &m_userMenuRows;
    labels = &m_userMenuLabels;
    if (m_selectedUser < m_users.size()) {
      for (std::size_t i = 0; i < m_userMenuFilteredIndices.size(); ++i) {
        if (m_userMenuFilteredIndices[i] == m_selectedUser) {
          selected = i;
          break;
        }
      }
    }
  }

  if (rows == nullptr || rows->empty()) {
    m_menuHighlight = -1;
    return;
  }

  const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(rows->size());
  if (m_userMenuOpen) {
    const std::size_t matchCount = m_userMenuFilteredIndices.size();
    if (matchCount == 0) {
      m_menuHighlight = -1;
      return;
    }
    if (m_menuHighlight < 0 || static_cast<std::size_t>(m_menuHighlight) >= matchCount) {
      m_menuHighlight = static_cast<std::ptrdiff_t>(std::min(selected, matchCount - 1));
    }
  } else if (m_menuHighlight < 0 || m_menuHighlight >= count) {
    m_menuHighlight = std::min(static_cast<std::ptrdiff_t>(selected), count - 1);
  }

  for (std::ptrdiff_t i = 0; i < count; ++i) {
    const std::size_t index = static_cast<std::size_t>(i);
    Box* row = (*rows)[index];
    if (row == nullptr) {
      continue;
    }

    const std::size_t filteredIndex = m_userMenuOpen ? m_userMenuScrollOffset + index : index;
    const bool highlighted =
        m_userMenuOpen ? filteredIndex == static_cast<std::size_t>(m_menuHighlight) : i == m_menuHighlight;
    const bool isSelected = m_userMenuOpen ? (filteredIndex < m_userMenuFilteredIndices.size()
                                              && m_userMenuFilteredIndices[filteredIndex] == m_selectedUser)
                                           : (index == selected);
    const float borderWidth = Style::borderWidth();
    const float panelRadius = Style::scaledRadiusLg();
    const float rowCorner = m_userMenuOpen ? Style::scaledRadiusSm() : panelRadius - borderWidth;
    Radii rowRadius(0.0f);
    if (m_userMenuOpen) {
      rowRadius = Radii(rowCorner);
    } else if (count == 1) {
      rowRadius = Radii(panelRadius - borderWidth);
    } else if (i == 0) {
      rowRadius = Radii(panelRadius - borderWidth, panelRadius - borderWidth, 0.0f, 0.0f);
    } else if (i == count - 1) {
      rowRadius = Radii(0.0f, 0.0f, panelRadius - borderWidth, panelRadius - borderWidth);
    }

    Color rowFill = highlighted ? colorForRole(ColorRole::Secondary) : colorForRole(ColorRole::SurfaceVariant);
    Color labelColor = colorForRole(ColorRole::OnSurface);
    Color iconColor = colorForRole(ColorRole::OnSurfaceVariant);
    if (m_userMenuOpen) {
      if (highlighted) {
        rowFill = colorForRole(ColorRole::Primary);
        labelColor = colorForRole(ColorRole::OnPrimary);
        iconColor = colorForRole(ColorRole::OnPrimary);
      } else if (isSelected) {
        rowFill = colorForRole(ColorRole::Primary, 0.16f);
        labelColor = colorForRole(ColorRole::Primary);
      } else {
        rowFill = colorForRole(ColorRole::SurfaceVariant, 0.01f);
      }
    } else if (highlighted) {
      labelColor = colorForRole(ColorRole::OnSecondary);
    } else if (isSelected) {
      labelColor = colorForRole(ColorRole::Primary);
    }

    row->setStyle(
        RoundedRectStyle{
            .fill = rowFill,
            .fillMode = FillMode::Solid,
            .radius = rowRadius,
        }
    );

    if (labels != nullptr && index < labels->size()) {
      Label* label = (*labels)[index];
      if (label == nullptr) {
        continue;
      }
      label->setColor(labelColor);
    }
    if (m_userMenuOpen && index < m_userMenuRowIcons.size() && m_userMenuRowIcons[index] != nullptr) {
      m_userMenuRowIcons[index]->setColor(iconColor);
    }
  }

  requestRedraw();
}

void GreeterSurface::moveMenuHighlight(int delta) {
  if (m_userMenuOpen) {
    const std::size_t count = m_userMenuFilteredIndices.size();
    if (count == 0) {
      return;
    }
    if (m_menuHighlight < 0) {
      m_menuHighlight = 0;
    }
    const std::size_t current = static_cast<std::size_t>(m_menuHighlight);
    const std::size_t next = (current + count + static_cast<std::size_t>(delta)) % count;
    m_menuHighlight = static_cast<std::ptrdiff_t>(next);
    const std::size_t previousScroll = m_userMenuScrollOffset;
    ensureUserMenuHighlightVisible();
    if (m_userMenuScrollOffset != previousScroll) {
      refreshUserMenuRows();
    } else {
      applyMenuHighlight();
      requestRedraw();
    }
    return;
  }

  std::vector<Box*>* rows = m_sessionMenuOpen ? &m_sessionMenuRows : m_schemeMenuOpen ? &m_schemeMenuRows : nullptr;
  if (rows == nullptr || rows->empty()) {
    return;
  }
  const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(rows->size());
  if (m_menuHighlight < 0) {
    m_menuHighlight = 0;
  }
  m_menuHighlight = (((m_menuHighlight + delta) % count) + count) % count;
  applyMenuHighlight();
  requestRedraw();
}

void GreeterSurface::ensureUserMenuHighlightVisible() {
  if (!m_userMenuOpen || m_userMenuFilteredIndices.empty()) {
    return;
  }

  const std::size_t matchCount = m_userMenuFilteredIndices.size();
  const std::size_t visibleCount = std::min(matchCount, kUserDropdownMaxVisibleRows);
  const std::size_t maxScroll = matchCount > visibleCount ? matchCount - visibleCount : 0;

  if (m_menuHighlight < 0) {
    m_menuHighlight = 0;
  }
  const std::size_t highlight = static_cast<std::size_t>(m_menuHighlight);
  if (highlight < m_userMenuScrollOffset) {
    m_userMenuScrollOffset = highlight;
  } else if (visibleCount > 0 && highlight >= m_userMenuScrollOffset + visibleCount) {
    m_userMenuScrollOffset = highlight + 1 - visibleCount;
  }
  m_userMenuScrollOffset = std::min(m_userMenuScrollOffset, maxScroll);
}

void GreeterSurface::scrollUserMenu(int delta) {
  if (!m_userMenuOpen || m_userMenuFilteredIndices.empty() || delta == 0) {
    return;
  }

  const std::size_t matchCount = m_userMenuFilteredIndices.size();
  const std::size_t visibleCount = std::min(matchCount, kUserDropdownMaxVisibleRows);
  const std::size_t maxScroll = matchCount > visibleCount ? matchCount - visibleCount : 0;
  if (maxScroll == 0) {
    return;
  }

  const std::size_t previousScroll = m_userMenuScrollOffset;
  if (delta < 0) {
    const std::size_t step = static_cast<std::size_t>(-delta);
    m_userMenuScrollOffset = step >= m_userMenuScrollOffset ? 0 : m_userMenuScrollOffset - step;
  } else {
    const std::size_t step = static_cast<std::size_t>(delta);
    m_userMenuScrollOffset = std::min(m_userMenuScrollOffset + step, maxScroll);
  }

  if (m_userMenuScrollOffset != previousScroll) {
    refreshUserMenuRows();
  }
}

bool GreeterSurface::pointerInUserMenuScrollArea(float x, float y) const {
  const auto inRect = [x, y](const Node* node) {
    if (node == nullptr || !node->visible()) {
      return false;
    }
    return x >= node->x() && y >= node->y() && x <= node->x() + node->width() && y <= node->y() + node->height();
  };

  return inRect(m_userMenuPanel) || inRect(m_userSelectBox);
}

void GreeterSurface::activateMenuHighlight() {
  if (m_menuHighlight < 0) {
    return;
  }
  const std::size_t idx = static_cast<std::size_t>(m_menuHighlight);
  if (m_sessionMenuOpen) {
    selectSession(idx);
  } else if (m_schemeMenuOpen) {
    selectScheme(idx);
  } else if (m_userMenuOpen) {
    if (idx < m_userMenuFilteredIndices.size()) {
      enterPasswordStep(m_userMenuFilteredIndices[idx]);
    }
  }
}

bool GreeterSurface::handleNavigationKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers) {
  if (m_authenticating) {
    return false;
  }
  const bool shift = (modifiers & KeyMod::Shift) != 0;
  const bool backward = shift || sym == XKB_KEY_ISO_Left_Tab;

  // F3 / F7 jump straight to the session / color-scheme menus from anywhere
  // (even from inside the password field or with another menu open).
  if (KeySymbol::isF3(sym)) {
    if (m_sessionMenuOpen) {
      closeMenusAndRestoreFocus();
    } else {
      if (m_sessionSelectArea != nullptr) {
        m_inputDispatcher.setFocus(m_sessionSelectArea);
      }
      toggleSessionMenu();
    }
    return true;
  }
  if (KeySymbol::isF7(sym)) {
    if (m_schemeMenuOpen) {
      closeMenusAndRestoreFocus();
    } else {
      if (m_schemeSelectArea != nullptr) {
        m_inputDispatcher.setFocus(m_schemeSelectArea);
      }
      toggleSchemeMenu();
    }
    return true;
  }

  // A dropdown menu being open takes total precedence over the focus ring.
  if (menuOpen()) {
    if (m_userMenuOpen) {
      InputArea* searchArea = m_userMenuSearchField != nullptr ? m_userMenuSearchField->inputArea() : nullptr;
      const bool searchFocused = searchArea != nullptr && InputArea::getFocused() == searchArea;

      if (isTextEditKey(sym, utf32, modifiers) && searchArea != nullptr) {
        if (!searchFocused) {
          m_inputDispatcher.setFocus(searchArea);
        }
        return false;
      }

      if (searchFocused) {
        if (handleUserMenuSearchKey(sym, false)) {
          return true;
        }
        return false;
      }
    }

    if (KeySymbol::isUp(sym)) {
      moveMenuHighlight(-1);
      return true;
    }
    if (KeySymbol::isDown(sym)) {
      moveMenuHighlight(1);
      return true;
    }
    if (KeySymbol::isEnter(sym)) {
      activateMenuHighlight();
      return true;
    }
    if (KeySymbol::isEscape(sym)) {
      closeMenusAndRestoreFocus();
      return true;
    }
    if (KeySymbol::isTab(sym)) {
      closeMenusAndRestoreFocus();
      moveFocus(backward ? -1 : 1);
      return true;
    }
    return true; // swallow everything else while a menu is open
  }

  // Tab / Shift+Tab cycles the focus ring, even from inside the password field.
  if (KeySymbol::isTab(sym)) {
    moveFocus(backward ? -1 : 1);
    return true;
  }

  // Escape leaves the password step.
  if (KeySymbol::isEscape(sym)) {
    if (m_passwordVisible) {
      runBackAction();
      return true;
    }
    return false;
  }

  // Let the password field keep all of its editing/submit keys.
  InputArea* focused = InputArea::getFocused();
  if (m_passwordField != nullptr && focused == m_passwordField->inputArea()) {
    return false;
  }

  if (!menuOpen()
      && !m_passwordVisible
      && showsUserDropdown()
      && m_userSelectArea != nullptr
      && focused == m_userSelectArea) {
    if (KeySymbol::isDown(sym) || KeySymbol::isUp(sym)) {
      openUserMenu();
      requestLayout();
      return true;
    }
  }

  // Non-text focusables: arrows move focus, Enter/Space activate.
  if (KeySymbol::isUp(sym)) {
    moveFocus(-1);
    return true;
  }
  if (KeySymbol::isDown(sym)) {
    moveFocus(1);
    return true;
  }
  if (KeySymbol::isEnter(sym) || KeySymbol::isSpace(sym)) {
    if (InputArea::getFocused() == nullptr && !m_focusRing.empty()) {
      setFocusIndex(defaultFocusIndex());
    }
    activateFocused();
    return true;
  }

  return false;
}

void GreeterSurface::clearUserMenuRows() {
  if (m_userMenuEmptyLabel != nullptr) {
    (void)m_root.removeChild(m_userMenuEmptyLabel);
    m_userMenuEmptyLabel = nullptr;
  }
  if (m_userMenuPanel != nullptr) {
    (void)m_root.removeChild(m_userMenuPanel);
    m_userMenuPanel = nullptr;
  }
  for (auto* label : m_userMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto* icon : m_userMenuRowIcons) {
    (void)m_root.removeChild(icon);
  }
  for (auto* area : m_userMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto* row : m_userMenuRows) {
    (void)m_root.removeChild(row);
  }
  m_userMenuLabels.clear();
  m_userMenuRowIcons.clear();
  m_userMenuAreas.clear();
  m_userMenuRows.clear();
}

void GreeterSurface::clearUserMenu() {
  m_inputDispatcher.invalidateTransientPointers();
  clearUserMenuRows();
  m_userMenuSearchQuery.clear();
  m_userMenuScrollOffset = 0;
  if (m_userMenuSearchField != nullptr) {
    m_userMenuSearchField->setValue("");
    m_userMenuSearchField->setVisible(false);
  }
  m_userMenuFilteredIndices.clear();
}

void GreeterSurface::clearSessionMenu() {
  m_inputDispatcher.invalidateTransientPointers();
  if (m_sessionMenuPanel != nullptr) {
    (void)m_root.removeChild(m_sessionMenuPanel);
    m_sessionMenuPanel = nullptr;
  }
  for (auto* label : m_sessionMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto* area : m_sessionMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto* row : m_sessionMenuRows) {
    (void)m_root.removeChild(row);
  }
  m_sessionMenuLabels.clear();
  m_sessionMenuAreas.clear();
  m_sessionMenuRows.clear();
}

void GreeterSurface::clearSchemeMenu() {
  m_inputDispatcher.invalidateTransientPointers();
  if (m_schemeMenuPanel != nullptr) {
    (void)m_root.removeChild(m_schemeMenuPanel);
    m_schemeMenuPanel = nullptr;
  }
  for (auto* label : m_schemeMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto* area : m_schemeMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto* row : m_schemeMenuRows) {
    (void)m_root.removeChild(row);
  }
  m_schemeMenuLabels.clear();
  m_schemeMenuAreas.clear();
  m_schemeMenuRows.clear();
}

bool GreeterSurface::handleUserMenuSearchKey(std::uint32_t sym, bool preedit) {
  if (preedit) {
    return false;
  }
  if (KeySymbol::isEscape(sym)) {
    closeMenusAndRestoreFocus();
    return true;
  }
  if (KeySymbol::isUp(sym)) {
    moveMenuHighlight(-1);
    return true;
  }
  if (KeySymbol::isDown(sym)) {
    moveMenuHighlight(1);
    return true;
  }
  if (KeySymbol::isEnter(sym)) {
    if (m_userMenuFilteredIndices.size() == 1) {
      enterPasswordStep(m_userMenuFilteredIndices.front());
      return true;
    }
    if (m_menuHighlight < 0 && !m_userMenuFilteredIndices.empty()) {
      m_menuHighlight = 0;
    }
    if (m_menuHighlight >= 0) {
      activateMenuHighlight();
      return true;
    }
  }
  return false;
}

void GreeterSurface::layoutUserMenuSearchField(float x, float y, float w, float h) {
  if (m_userMenuSearchField == nullptr || m_renderContext == nullptr) {
    return;
  }

  if (m_userMenuSearchField->value() != m_userMenuSearchQuery) {
    m_userMenuSearchField->setValue(m_userMenuSearchQuery);
  }
  m_userMenuSearchField->setControlHeight(h);
  m_userMenuSearchField->setPosition(x, y);
  m_userMenuSearchField->setSize(w, h);
  m_userMenuSearchField->layout(*m_renderContext);
}

void GreeterSurface::refreshUserMenuRows() {
  if (!m_userMenuOpen || m_users.empty() || m_userSelectBox == nullptr || m_renderContext == nullptr) {
    return;
  }

  auto* renderer = m_renderContext;
  m_userMenuFilteredIndices = filterUserIndices(m_users, m_userMenuSearchQuery);

  const float anchorX = m_userSelectBox->x();
  const float anchorW = m_userSelectBox->width();
  const float rowH = Style::controlHeight();
  const float borderWidth = Style::borderWidth();
  const float panelRadius = Style::scaledRadiusLg();
  const float rowRadius = Style::scaledRadiusSm();
  const float rowIconReserve = Style::spaceMd() + Style::fontSizeBody() + kUserMenuRowIconGap;

  float contentW = anchorW;
  for (const std::size_t userIndex : m_userMenuFilteredIndices) {
    const float textW = renderer->measureText(m_users[userIndex], Style::fontSizeBody()).width;
    contentW = std::max(contentW, rowIconReserve + textW + 2.0f * kUserMenuPadding);
  }
  const float screenW = m_root.width();
  const float maxW = std::max(anchorW, screenW - 2.0f * Style::spaceLg());
  const float panelW = std::min(contentW, maxW);
  const float panelX =
      std::clamp(anchorX, Style::spaceLg(), std::max(Style::spaceLg(), screenW - Style::spaceLg() - panelW));
  const float panelY = m_userSelectBox->y() + m_userSelectBox->height() + Style::spaceXs();
  const float innerX = panelX + borderWidth + kUserMenuPadding;
  const float innerW = panelW - 2.0f * borderWidth - 2.0f * kUserMenuPadding;
  const float listTop = panelY + borderWidth + kUserMenuPadding;
  clearUserMenuRows();

  const std::size_t matchCount = m_userMenuFilteredIndices.size();
  if (matchCount == 0) {
    m_userMenuScrollOffset = 0;
    const float panelH = kUserMenuPadding + rowH + kUserMenuPadding + 2.0f * borderWidth;

    auto panel = std::make_unique<Box>();
    m_userMenuPanel = panel.get();
    m_userMenuPanel->setZIndex(50);
    m_userMenuPanel->setHitTestVisible(false);
    m_root.addChild(std::move(panel));
    m_userMenuPanel->setPosition(panelX, panelY);
    m_userMenuPanel->setSize(panelW, panelH);
    m_userMenuPanel->setStyle(
        RoundedRectStyle{
            .fill = colorForRole(ColorRole::Surface),
            .border = colorForRole(ColorRole::Outline),
            .fillMode = FillMode::Solid,
            .radius = panelRadius,
            .softness = 1.0f,
            .borderWidth = borderWidth,
        }
    );

    auto empty = std::make_unique<Label>();
    m_userMenuEmptyLabel = empty.get();
    m_userMenuEmptyLabel->setText("No users found");
    m_userMenuEmptyLabel->setFontSize(Style::fontSizeBody());
    m_userMenuEmptyLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    m_userMenuEmptyLabel->setZIndex(52);
    m_root.addChild(std::move(empty));
    m_userMenuEmptyLabel->measure(*renderer);
    m_userMenuEmptyLabel->setPosition(innerX, listTop + std::round((rowH - m_userMenuEmptyLabel->height()) * 0.5f));
    m_menuHighlight = -1;
    applyMenuHighlight();
    requestRedraw();
    return;
  }

  if (m_menuHighlight < 0 || static_cast<std::size_t>(m_menuHighlight) >= matchCount) {
    m_menuHighlight = 0;
    if (m_selectedUser < m_users.size()) {
      for (std::size_t i = 0; i < matchCount; ++i) {
        if (m_userMenuFilteredIndices[i] == m_selectedUser) {
          m_menuHighlight = static_cast<std::ptrdiff_t>(i);
          break;
        }
      }
    }
  }
  ensureUserMenuHighlightVisible();

  const std::size_t visibleCount = std::min(kUserDropdownMaxVisibleRows, matchCount - m_userMenuScrollOffset);
  const float listH = rowH * static_cast<float>(visibleCount)
      + kUserMenuRowGap * static_cast<float>(visibleCount > 0 ? visibleCount - 1 : 0);
  const float panelH = kUserMenuPadding + listH + kUserMenuPadding + 2.0f * borderWidth;

  auto panel = std::make_unique<Box>();
  m_userMenuPanel = panel.get();
  m_userMenuPanel->setZIndex(50);
  m_userMenuPanel->setHitTestVisible(false);
  m_root.addChild(std::move(panel));
  m_userMenuPanel->setPosition(panelX, panelY);
  m_userMenuPanel->setSize(panelW, panelH);
  m_userMenuPanel->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Surface),
          .border = colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = panelRadius,
          .softness = 1.0f,
          .borderWidth = borderWidth,
      }
  );

  for (std::size_t visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex) {
    const std::size_t filteredIndex = m_userMenuScrollOffset + visibleIndex;
    const std::size_t userIndex = m_userMenuFilteredIndices[filteredIndex];
    const float rowY = listTop + (rowH + kUserMenuRowGap) * static_cast<float>(visibleIndex);
    const float rowW = innerW;

    auto row = std::make_unique<Box>();
    auto* rowPtr = row.get();
    rowPtr->setZIndex(51);
    rowPtr->setHitTestVisible(false);
    rowPtr->setPosition(innerX, rowY);
    rowPtr->setSize(rowW, rowH);
    rowPtr->setStyle(
        RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
            .fillMode = FillMode::Solid,
            .radius = rowRadius,
        }
    );
    m_root.addChild(std::move(row));
    m_userMenuRows.push_back(rowPtr);

    auto icon = std::make_unique<Glyph>();
    auto* iconPtr = icon.get();
    iconPtr->setGlyph("user");
    iconPtr->setGlyphSize(Style::fontSizeBody());
    iconPtr->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    iconPtr->setHitTestVisible(false);
    iconPtr->setZIndex(52);
    m_root.addChild(std::move(icon));
    (void)iconPtr->measure(*renderer);
    const auto iconMetrics = renderer->measureGlyph(iconPtr->codepoint(), iconPtr->fontSize());
    const float iconW = iconMetrics.right - iconMetrics.left;
    const float iconY = rowY + std::round(rowH * 0.5f - (iconMetrics.top + iconMetrics.bottom) * 0.5f);
    iconPtr->setPosition(innerX + Style::spaceSm(), iconY);
    m_userMenuRowIcons.push_back(iconPtr);

    auto label = std::make_unique<Label>();
    auto* labelPtr = label.get();
    labelPtr->setText(m_users[userIndex]);
    labelPtr->setFontSize(Style::fontSizeBody());
    labelPtr->setColor(colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(52);
    m_root.addChild(std::move(label));
    labelPtr->measure(*renderer);
    labelPtr->setPosition(
        innerX + Style::spaceSm() + iconW + kUserMenuRowIconGap, rowY + std::round((rowH - labelPtr->height()) * 0.5f)
    );
    m_userMenuLabels.push_back(labelPtr);

    auto area = std::make_unique<InputArea>();
    auto* areaPtr = area.get();
    areaPtr->setFocusable(false);
    areaPtr->setZIndex(53);
    areaPtr->setOnEnter([this, filteredIndex](const InputArea::PointerData&) {
      m_menuHighlight = static_cast<std::ptrdiff_t>(filteredIndex);
      applyMenuHighlight();
    });
    areaPtr->setOnPress([this, userIndex](const InputArea::PointerData& data) {
      if (!data.pressed || data.button != BTN_LEFT) {
        return;
      }
      enterPasswordStep(userIndex);
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(innerX, rowY);
    areaPtr->setSize(rowW, rowH);
    m_userMenuAreas.push_back(areaPtr);
  }

  applyMenuHighlight();
  requestRedraw();
}

void GreeterSurface::rebuildUserMenu() {
  if (!m_userMenuOpen || m_users.empty()) {
    clearUserMenu();
    return;
  }

  refreshUserMenuRows();
}

void GreeterSurface::buildMenu(
    const std::vector<std::string>& names, std::size_t selected, Box* anchor, bool upward, bool rightAlign, int zBase,
    Box*& panelOut, std::vector<Box*>& rows, std::vector<Label*>& labels, std::vector<InputArea*>& areas,
    std::function<void(std::size_t)> onSelect
) {
  const std::size_t count = names.size();
  if (anchor == nullptr || count == 0) {
    return;
  }
  (void)selected;

  const float rowH = Style::controlHeightSm();
  const float anchorX = anchor->x();
  const float anchorW = anchor->width();
  const float h = rowH * static_cast<float>(count);

  // Create and measure the labels first so the panel can be widened to fit the
  // longest entry (session/scheme names are often wider than the selector).
  float contentW = anchorW;
  for (std::size_t i = 0; i < count; ++i) {
    auto label = std::make_unique<Label>();
    auto* labelPtr = label.get();
    labelPtr->setText(names[i]);
    labelPtr->setFontSize(Style::fontSizeBody());
    labelPtr->setColor(colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(zBase + 2);
    m_root.addChild(std::move(label));
    labelPtr->measure(*m_renderContext);
    contentW = std::max(contentW, labelPtr->width() + 2.0f * Style::spaceMd());
    labels.push_back(labelPtr);
  }

  // Fit the content, but stay within the screen and never narrower than the
  // selector.
  const float screenW = m_root.width();
  const float maxW = std::max(anchorW, screenW - 2.0f * Style::spaceLg());
  const float w = std::min(contentW, maxW);
  const float maxX = std::max(Style::spaceLg(), screenW - Style::spaceLg() - w);
  float x = rightAlign ? (anchorX + anchorW - w) : anchorX;
  x = std::clamp(x, Style::spaceLg(), maxX);

  const float borderWidth = Style::borderWidth();
  const float panelH = h + 2.0f * borderWidth;
  const float y =
      upward ? (anchor->y() - panelH - Style::spaceXs()) : (anchor->y() + anchor->height() + Style::spaceXs());

  auto panel = std::make_unique<Box>();
  panelOut = panel.get();
  panelOut->setZIndex(zBase);
  m_root.addChild(std::move(panel));
  panelOut->setPosition(x, y);
  panelOut->setSize(w, panelH);
  panelOut->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::SurfaceVariant),
          .border = colorForRole(ColorRole::Outline),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = borderWidth,
      }
  );

  const float panelRadius = Style::scaledRadiusLg();

  for (std::size_t i = 0; i < count; ++i) {
    const float rowX = x + borderWidth;
    const float rowY = y + borderWidth + rowH * static_cast<float>(i);
    const float rowW = w - 2.0f * borderWidth;

    auto row = std::make_unique<Box>();
    auto* rowPtr = row.get();
    rowPtr->setZIndex(zBase + 1);
    rowPtr->setPosition(rowX, rowY);
    rowPtr->setSize(rowW, rowH);

    Radii rowRadius(0.0f);
    if (count == 1) {
      rowRadius = Radii(panelRadius - borderWidth);
    } else if (i == 0) {
      rowRadius = Radii(panelRadius - borderWidth, panelRadius - borderWidth, 0.0f, 0.0f);
    } else if (i == count - 1) {
      rowRadius = Radii(0.0f, 0.0f, panelRadius - borderWidth, panelRadius - borderWidth);
    }

    rowPtr->setStyle(
        RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant),
            .fillMode = FillMode::Solid,
            .radius = rowRadius,
        }
    );
    m_root.addChild(std::move(row));
    rows.push_back(rowPtr);

    Label* labelPtr = labels[i];
    labelPtr->setPosition(rowX + Style::spaceMd(), rowY + std::round((rowH - labelPtr->height()) * 0.5f));

    auto area = std::make_unique<InputArea>();
    auto* areaPtr = area.get();
    areaPtr->setFocusable(true);
    areaPtr->setZIndex(zBase + 3);
    areaPtr->setOnEnter([this, i](const InputArea::PointerData&) {
      m_menuHighlight = static_cast<std::ptrdiff_t>(i);
      applyMenuHighlight();
      requestRedraw();
    });
    areaPtr->setOnClick([onSelect, i](const InputArea::PointerData& data) {
      if (data.button != BTN_LEFT) {
        return;
      }
      onSelect(i);
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(rowX, rowY);
    areaPtr->setSize(rowW, rowH);
    areas.push_back(areaPtr);
  }
}

void GreeterSurface::rebuildSessionMenu() {
  clearSessionMenu();
  if (!m_sessionMenuOpen || m_sessions.empty()) {
    return;
  }
  std::vector<std::string> names;
  names.reserve(m_sessions.size());
  for (const auto& session : m_sessions) {
    names.push_back(session.name);
  }
  buildMenu(
      names, m_selectedSession, m_sessionSelectBox, /*upward=*/false,
      /*rightAlign=*/false, /*zBase=*/50, m_sessionMenuPanel, m_sessionMenuRows, m_sessionMenuLabels,
      m_sessionMenuAreas, [this](std::size_t i) { selectSession(i); }
  );
}

void GreeterSurface::rebuildSchemeMenu() {
  clearSchemeMenu();
  if (!m_schemeMenuOpen || m_schemeNames.empty()) {
    return;
  }
  buildMenu(
      m_schemeNames, m_selectedScheme, m_schemeSelectBox,
      /*upward=*/false,
      /*rightAlign=*/true, /*zBase=*/60, m_schemeMenuPanel, m_schemeMenuRows, m_schemeMenuLabels, m_schemeMenuAreas,
      [this](std::size_t i) { selectScheme(i); }
  );
}
