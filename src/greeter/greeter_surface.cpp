#include "greeter/greeter_surface.h"

#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_sessions.h"
#include "greeter/greeter_window.h"
#include "greeter/power_actions.h"
#include "greeter/render/image_loader.h"
#include "greeter/render/wallpaper_node.h"
#include "greeter/resources.h"
#include "greeter/ui/greeter_components.h"
#include "greeter/ui/greeter_metrics.h"
#include "greeter/ui/search_field.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/render_context.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "render/scene/rect_node.h"
#include "theme/builtin_palettes.h"
#include "time/time_format.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scrollbar.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <sstream>
#include <wayland-client.h>

namespace {
  constexpr Logger kLog("greeter-surface");
  constexpr float kHeaderAvatarBorderScale = 2.0f;
  // Blur taps at quarter resolution; matches the shader's 40-tap cap.
  constexpr float kWallpaperMaxBlurRadius = 40.0f;

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
        .borderWidth = Style::borderWidth,
        .normal = makePaletteState(ColorRole::Surface, ColorRole::OutlineVariant, ColorRole::OnSurface, 0.78f),
        .hover = makePaletteState(hover, std::nullopt, onHover),
        .pressed = makePaletteState(hover, hover, onHover),
        .disabled = makePaletteState(ColorRole::Surface, ColorRole::OutlineVariant, ColorRole::OnSurface, 0.40f),
        .selected = std::nullopt,
    };
  }

  constexpr std::size_t kUserDropdownMaxVisibleRows = 8;
  constexpr std::size_t kSelectorMenuMaxVisibleRows = 8;
  const float kUserMenuPadding = Style::spaceSm;
  const float kUserMenuRowGap = Style::spaceXs;
  const float kUserMenuRowIconGap = Style::spaceSm;

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
            || sym == XKB_KEY_X
            || sym == 'u'
            || sym == 'U'
            || sym == XKB_KEY_u
            || sym == XKB_KEY_U);
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

GreeterSurface::GreeterSurface(GreeterModel& model, GreeterAuthController& auth, GreeterViewId viewId)
    : m_model(model), m_auth(auth), m_viewId(viewId) {}

GreeterSurface::~GreeterSurface() {
  if (m_renderContext != nullptr) {
    if (m_brandLogoTexture.id != 0) {
      m_renderContext->textureManager().unload(m_brandLogoTexture);
    }
    if (m_wallpaperTexture.id != 0) {
      m_renderContext->textureManager().unload(m_wallpaperTexture);
    }
    if (m_headerAvatarTexture.id != 0)
      m_renderContext->textureManager().unload(m_headerAvatarTexture);
  }
}

void GreeterSurface::initialize(RenderContext* context) {
  m_renderContext = context;

  auto wallpaper = std::make_unique<WallpaperNode>();
  m_wallpaper = wallpaper.get();
  m_wallpaper->setZIndex(0);
  m_wallpaper->setSpan(m_wallpaperSpanParams);
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
  m_brandLogo = bottomLogo.get();
  m_root.addChild(std::move(bottomLogo));

  auto clockTime = std::make_unique<Label>();
  clockTime->setFontSize(m_clockTimeFontSize);
  clockTime->setTextAlign(TextAlign::Center);
  clockTime->setColor(colorForRole(ColorRole::OnSurface));
  clockTime->setVisible(false);
  m_clockTimeLabel = clockTime.get();
  m_clockTimeLabel->setZIndex(5);
  m_root.addChild(std::move(clockTime));

  auto clockDate = std::make_unique<Label>();
  clockDate->setFontSize(m_clockDateFontSize);
  clockDate->setTextAlign(TextAlign::Center);
  clockDate->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  clockDate->setVisible(false);
  m_clockDateLabel = clockDate.get();
  m_clockDateLabel->setZIndex(5);
  m_root.addChild(std::move(clockDate));

  auto formSubtitle = std::make_unique<Label>();
  formSubtitle->setFontSize(Style::fontSizeTitle);
  formSubtitle->setFontWeight(FontWeight::Bold);
  formSubtitle->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_identityLabel = formSubtitle.get();
  m_identityLabel->setZIndex(6);
  m_root.addChild(std::move(formSubtitle));

  auto headerGlyph = std::make_unique<Glyph>();
  headerGlyph->setGlyph("user");
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

  auto divider = std::make_unique<RectNode>();
  divider->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::OutlineVariant, 0.90f),
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
  pwField->setControlHeight(GreeterMetrics::accountRowHeight);
  pwField->setOnSubmit([this](const std::string&) { tryAuthenticate(); });
  m_passwordField = pwField.get();
  m_passwordField->setZIndex(6);
  m_root.addChild(std::move(pwField));

  auto userBox = std::make_unique<Box>();
  m_userSelectBox = userBox.get();
  m_userSelectBox->setZIndex(6);
  m_root.addChild(std::move(userBox));

  auto userLabel = std::make_unique<Label>();
  userLabel->setFontSize(Style::fontSizeBody);
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
  m_userSelectArea->setOnFocusGain([this]() {
    syncPanelUserChrome();
    requestRedraw();
  });
  m_userSelectArea->setOnFocusLoss([this]() {
    syncPanelUserChrome();
    requestRedraw();
  });
  m_root.addChild(std::move(userArea));

  auto userSearch = std::make_unique<SearchField>();
  userSearch->setPlaceholder("Search users…");
  userSearch->setFontSize(Style::fontSizeBody);
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

  auto sessionLabel = std::make_unique<Label>();
  sessionLabel->setFontSize(Style::fontSizeBody);
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
  m_sessionSelectArea->setOnFocusGain([this]() {
    syncPanelSessionChrome();
    requestRedraw();
  });
  m_sessionSelectArea->setOnFocusLoss([this]() {
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
  schemeIcon->setGlyphSize(Style::fontSizeBody);
  schemeIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  schemeIcon->setHitTestVisible(false);
  m_schemeSelectIcon = schemeIcon.get();
  m_schemeSelectIcon->setZIndex(6);
  m_root.addChild(std::move(schemeIcon));

  auto schemeLabel = std::make_unique<Label>();
  schemeLabel->setFontSize(Style::fontSizeBody);
  m_schemeSelectLabel = schemeLabel.get();
  m_schemeSelectLabel->setZIndex(6);
  m_root.addChild(std::move(schemeLabel));

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
  m_schemeSelectArea->setOnFocusGain([this]() {
    applySelectorBoxStyle(m_schemeSelectBox, m_schemeSelectArea);
    requestRedraw();
  });
  m_schemeSelectArea->setOnFocusLoss([this]() {
    applySelectorBoxStyle(m_schemeSelectBox, m_schemeSelectArea);
    requestRedraw();
  });
  m_root.addChild(std::move(schemeArea));

  auto loginBtn = std::make_unique<Button>();
  loginBtn->setVariant(ButtonVariant::Primary);
  loginBtn->setContentAlign(ButtonContentAlign::Center);
  loginBtn->setOnClick([this]() { tryAuthenticate(); });
  m_loginButton = loginBtn.get();
  m_loginButton->setZIndex(6);
  m_loginButton->setAnimationManager(nullptr);
  m_root.addChild(std::move(loginBtn));

  auto status = std::make_unique<Label>();
  status->setFontSize(GreeterMetrics::captionFontSize);
  status->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_statusLabel = status.get();
  m_statusLabel->setZIndex(6);
  m_root.addChild(std::move(status));

  auto configErrorBanner = std::make_unique<RectNode>();
  configErrorBanner->setHitTestVisible(false);
  configErrorBanner->setVisible(false);
  m_configErrorBanner = configErrorBanner.get();
  m_configErrorBanner->setZIndex(9);
  m_root.addChild(std::move(configErrorBanner));

  auto configErrorHeading = std::make_unique<Label>();
  configErrorHeading->setText("Configuration errors");
  configErrorHeading->setFontSize(GreeterMetrics::captionFontSize);
  configErrorHeading->setFontWeight(FontWeight::Bold);
  configErrorHeading->setColor(colorForRole(ColorRole::Error));
  configErrorHeading->setVisible(false);
  m_configErrorHeading = configErrorHeading.get();
  m_configErrorHeading->setZIndex(10);
  m_root.addChild(std::move(configErrorHeading));

  auto configError = std::make_unique<Label>();
  configError->setFontSize(GreeterMetrics::captionFontSize);
  configError->setColor(colorForRole(ColorRole::OnSurface));
  configError->setMaxLines(4);
  configError->setVisible(false);
  m_configErrorLabel = configError.get();
  m_configErrorLabel->setZIndex(10);
  m_root.addChild(std::move(configError));

  const bool canRebootToFirmware = power::canRebootToFirmwareSetup();

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

  m_shutdownButton =
      makePowerButton("power", ColorRole::Error, ColorRole::OnError, [this]() { requestPowerAction(3); });
  m_shutdownButton->setTooltip("Shut down");
  m_rebootButton =
      makePowerButton("reload", ColorRole::Secondary, ColorRole::OnSecondary, [this]() { requestPowerAction(2); });
  m_rebootButton->setTooltip("Restart");
  if (canRebootToFirmware) {
    m_firmwareButton =
        makePowerButton("cpu", ColorRole::Secondary, ColorRole::OnSecondary, [this]() { requestPowerAction(4); });
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

  m_loginComponent = std::make_unique<GreeterLoginPanel>(
      m_root, LoginPanelNodes{m_loginPanel, m_passwordField, m_loginButton, m_statusLabel, m_identityLabel}
  );
  m_chromeComponent = std::make_unique<GreeterChrome>(m_root);
  auto secondary = std::make_unique<Label>();
  m_userSecondaryLabel = secondary.get();
  secondary->setZIndex(6);
  m_root.addChild(std::move(secondary));
  const auto addButton = [this](std::string_view label, std::string_view glyph, std::function<void()> callback) {
    auto button = std::make_unique<Button>();
    auto* ptr = button.get();
    button->setText(label);
    if (!glyph.empty())
      button->setGlyph(glyph);
    button->setVariant(ButtonVariant::Ghost);
    button->setFontSize(14);
    button->setGlyphSize(17);
    button->setOnClick(std::move(callback));
    button->setZIndex(102);
    m_root.addChild(std::move(button));
    return ptr;
  };
  auto caption = std::make_unique<Label>();
  m_sessionCaptionLabel = caption.get();
  caption->setZIndex(6);
  m_root.addChild(std::move(caption));
  m_powerMenuButton = addButton("Power", "power", [this] { togglePowerMenu(); });
  m_powerMenuButton->setZIndex(8);
  m_suspendButton = addButton(
      power::syncedActionLabel("suspend").value_or("Suspend"), power::syncedActionGlyph("suspend").value_or("moon"),
      [this] { requestPowerAction(1); }
  );
  m_powerCancel = addButton("Cancel", "arrow-left", [this] { closePowerMenu(); });
  m_powerConfirm = addButton("Confirm", "", [this] {
    const int action = m_pendingPowerAction;
    closePowerMenu();
    if (action == 1)
      power::suspend();
    else if (action == 2)
      power::reboot();
    else if (action == 3)
      power::powerOff();
    else if (action == 4)
      power::rebootToFirmwareSetup();
  });
  auto powerPanel = std::make_unique<Box>();
  m_powerPanel = powerPanel.get();
  powerPanel->setZIndex(100);
  powerPanel->setHitTestVisible(false);
  m_root.addChild(std::move(powerPanel));
  auto powerTitle = std::make_unique<Label>();
  m_powerTitle = powerTitle.get();
  powerTitle->setZIndex(102);
  m_root.addChild(std::move(powerTitle));
  m_shutdownButton->setText(power::syncedActionLabel("shutdown").value_or("Shut down…"));
  m_shutdownButton->setOnClick([this] { requestPowerAction(3); });
  m_rebootButton->setText(power::syncedActionLabel("reboot").value_or("Restart…"));
  m_rebootButton->setOnClick([this] { requestPowerAction(2); });
  if (m_firmwareButton) {
    m_firmwareButton->setText("Restart to firmware…");
    m_firmwareButton->setOnClick([this] { requestPowerAction(4); });
  }
  m_loginButton->setOnClick([this] {
    if (m_model.state().passwordVisible)
      tryAuthenticate();
    else
      openUserMenu();
  });
  m_syncedAppearance = m_model.syncedAppearance();
  m_wallpaperAppearance = loadGreeterWallpaperAppearance();
  loadPreferences();
  (void)updateClock();
  applyScheme(m_model.state().selectedScheme);
  refreshSelectionLabels();
  syncModel(false);

  if (const auto& diagnostics = greeter::config::configDiagnostics(); !diagnostics.empty()) {
    const auto& diagnostic = diagnostics.front();
    std::ostringstream message;
    message << diagnostic.path.filename().string();
    if (diagnostic.line > 0) {
      message << ':' << diagnostic.line;
      if (diagnostic.column > 0) {
        message << ':' << diagnostic.column;
      }
    }
    message << ": " << diagnostic.message;
    if (diagnostics.size() > 1) {
      message << " (+" << diagnostics.size() - 1 << " more)";
    }
    m_configErrorLabel->setText(message.str());
    m_configErrorLabel->setVisible(true);
    m_configErrorHeading->setVisible(true);
    m_configErrorBanner->setVisible(true);
  }

  if (m_layoutConfig.components[2].visible.value_or(true)) {
    const auto logoPath = paths::assetPath("noctalia.svg");
    m_brandLogoTexture = loadGreeterTexture(m_renderContext->textureManager(), logoPath.string(), 1024, true);
    if (m_brandLogo != nullptr && m_brandLogoTexture.id != 0) {
      m_brandLogo->setTextureId(m_brandLogoTexture.id);
      m_brandLogo->setTextureSize(m_brandLogoTexture.width, m_brandLogoTexture.height);
      // Keep the multicolor brand artwork intact. Palette tinting, especially
      // with a light theme's dark on-surface color, crushes its colors.
      m_brandLogo->setTint(rgba(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
      kLog.warn("failed loading logo texture from {}", logoPath.string());
    }
  }

  requestLayout();
}

bool GreeterSurface::showsUserDropdown() const noexcept { return m_model.users().size() > 1; }

void GreeterSurface::setKeyboardOwner(const bool owner) noexcept {
  if (m_isKeyboardOwner == owner)
    return;
  m_isKeyboardOwner = owner;
  if (owner) {
    reconcileKeyboardFocus();
  } else {
    m_inputDispatcher.setFocus(nullptr);
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
  InputArea* focused = m_inputDispatcher.focusedArea();
  if (focused != nullptr && ownsInputArea(focused)) {
    syncFocusIndexFromFocused();
    return;
  }

  if (m_model.state().passwordVisible && m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
    syncFocusIndexFromFocused();
    return;
  }

  if (!m_focusRing.empty()) {
    setFocusIndex(defaultFocusIndex());
  }
}

void GreeterSurface::setWindow(GreeterWindow* window) {
  m_window = window;
  m_renderer = window ? &window->renderTarget().renderer() : nullptr;
}

void GreeterSurface::syncModel(bool closeSelectionMenus) {
  if (m_renderContext == nullptr) {
    return;
  }
  const auto& state = m_model.state();
  if (m_credentialGeneration != state.credentialGeneration) {
    m_passwordField->setValue("");
    m_credentialGeneration = state.credentialGeneration;
  }
  if (m_appliedScheme != state.selectedScheme) {
    applyScheme(state.selectedScheme);
  }
  if (closeSelectionMenus) {
    closeMenus();
  }
  m_passwordField->setPasswordMode(!(state.waitingForInput && state.promptType == GreetdAuthMessageType::Visible));
  m_passwordField->setPlaceholder(
      !state.passwordVisible                                                            ? "Choose an account first"
          : state.waitingForInput && state.promptType == GreetdAuthMessageType::Visible ? "Enter your response"
                                                                                        : "Enter your password"
  );
  m_statusLabel->setText(state.status);
  m_statusLabel->setColor(colorForRole(state.statusIsError ? ColorRole::Error : ColorRole::OnSurfaceVariant));
  const bool enabled = !m_auth.inputBlocked(m_viewId);
  m_passwordField->setEnabled(enabled && state.passwordVisible);
  m_loginButton->setEnabled(enabled);
  refreshSelectionLabels();
  commitImmediateFrame(true);
}

void GreeterSurface::tryAuthenticate() { m_auth.submit(m_viewId, m_passwordField->value()); }

void GreeterSurface::onPointerLeave() {
  m_inputDispatcher.pointerLeave();
  requestRedraw();
}

void GreeterSurface::onPointerEvent(float x, float y, std::uint32_t button, bool pressed) {
  const float sceneX = x / m_sceneScale, sceneY = y / m_sceneScale;
  if (m_powerMenuOpen) {
    bool hit = false;
    for (Button* b :
         {m_suspendButton, m_rebootButton, m_shutdownButton, m_firmwareButton, m_powerCancel, m_powerConfirm})
      if (b
          && b->visible()
          && sceneX >= b->x()
          && sceneX <= b->x() + b->width()
          && sceneY >= b->y()
          && sceneY <= b->y() + b->height())
        hit = true;
    if (!hit) {
      if (pressed && button == BTN_LEFT)
        closePowerMenu();
      return;
    }
  }
  if (pressed && button == BTN_LEFT && (m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen)) {
    const auto inRect = [x = sceneX, y = sceneY](const Node* node) {
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
      if (m_userMenuOpen) {
        closeMenusAndRestoreFocus();
        return;
      }
      closeMenus();
    }
  }

  // Decorative/background clicks must not blur the current login control.
  // Still deliver releases for an active capture (text selection, scrollbar, …).
  if (!m_inputDispatcher.pointerCaptured() && m_inputDispatcher.inputAreaAt(x, y) == nullptr)
    return;
  m_inInputDispatch = true;
  m_inputDispatcher.pointerButton(x, y, button, pressed, 0, 0, false);
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
  if (axisLines == 0.0f || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
    return;
  }
  x /= m_sceneScale;
  y /= m_sceneScale;
  if (m_userMenuOpen && pointerInUserMenuScrollArea(x, y)) {
    scrollUserMenu(static_cast<int>(std::round(axisLines)));
  } else if (activeMenuScroll() != nullptr) {
    const Box* panel = m_sessionMenuOpen ? m_sessionMenuPanel : m_schemeMenuPanel;
    if (panel == nullptr
        || x < panel->x()
        || x > panel->x() + panel->width()
        || y < panel->y()
        || y > panel->y() + panel->height()) {
      return;
    }
    m_menuScrollRemainder += axisLines;
    const int delta = static_cast<int>(m_menuScrollRemainder);
    m_menuScrollRemainder -= static_cast<float>(delta);
    scrollSelectorMenu(delta);
  }
}

void GreeterSurface::onKeyEvent(
    std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit
) {
  if (!pressed)
    return;
  reconcileKeyboardFocus();
  m_inInputDispatch = true;
  // A focused shared Input can still receive key events when disabled. Keep
  // credentials out of it while another view owns auth or a reply is pending.
  const bool blockedCredential = m_passwordField != nullptr
      && m_inputDispatcher.focusedArea() == m_passwordField->inputArea()
      && (m_auth.inputBlocked(m_viewId) || !m_model.state().passwordVisible);
  if (!handleNavigationKey(sym, utf32, modifiers) && !blockedCredential) {
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

  if (needsLayout) {
    m_renderer = &m_window->renderTarget().renderer();
    syncWallpaperTexture();
    m_inLayout = true;
    layoutScene(width, height);
    m_inLayout = false;
  }
}

bool GreeterSurface::updateClock() {
  if (m_clockTimeLabel == nullptr || m_clockDateLabel == nullptr) {
    return false;
  }

  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  const std::int64_t second = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  if (m_lastClockSecond.has_value() && *m_lastClockSecond == second) {
    return false;
  }
  m_lastClockSecond = second;

  const std::string time = m_clockEnabled ? formatLocalUnixTime(second, m_clockTimeFormat) : std::string{};
  const std::string date = m_clockEnabled ? formatLocalUnixTime(second, m_clockDateFormat) : std::string{};
  const bool timeChanged = m_clockTimeLabel->setText(time);
  const bool dateChanged = m_clockDateLabel->setText(date);
  return timeChanged || dateChanged;
}

void GreeterSurface::enterPasswordStep(std::size_t userIndex) {
  if (!m_auth.selectUser(m_viewId, userIndex)) {
    return;
  }
  m_userMenuOpen = false;
  m_userMenuSearchQuery.clear();
  if (m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
  }
  refreshSelectionLabels();
  commitImmediateFrame(true);
}

bool GreeterSurface::minimalLogin() const {
  return m_layoutConfig.loginPresentation != greeter::LoginPresentation::Full
      && m_model.hasConfiguredDefaults()
      && m_model.state().passwordVisible;
}
void GreeterSurface::setKeyboardLayoutName(std::string name) {
  if (name != m_keyboardLayout) {
    m_keyboardLayout = std::move(name);
    requestLayout();
  }
}

void GreeterSurface::layoutScene(std::uint32_t width, std::uint32_t height) {
  if (!m_renderer || !m_loginComponent || !m_chromeComponent)
    return;
  auto& renderer = *m_renderer;
  m_sceneScale = std::min({1.0F, static_cast<float>(width) / 320, static_cast<float>(height) / 520});
  const float fullW = static_cast<float>(width) / m_sceneScale, fullH = static_cast<float>(height) / m_sceneScale;
  m_root.setScale(m_sceneScale);
  m_root.setTranslate(-fullW * (1 - m_sceneScale) / 2, -fullH * (1 - m_sceneScale) / 2);
  const float sw = fullW, sh = fullH;
  m_root.setSize(fullW, fullH);
  m_wallpaper->setPosition(0, 0);
  m_wallpaper->setSize(sw, sh);
  m_wallpaper->setFillMode(m_wallpaperFillMode);
  m_wallpaper->setFillColor(m_wallpaperFillColor);
  m_wallpaper->setBlurRadius(m_wallpaperBlur * kWallpaperMaxBlurRadius);
  m_wallpaper->setTintColor(colorForRole(ColorRole::Surface, m_wallpaperTint));
  m_backdrop->setPosition(0, 0);
  m_backdrop->setSize(sw, sh);
  m_backdrop->setVisible(!m_hasWallpaper);
  m_backdrop->setStyle({.fill = colorForRole(ColorRole::Surface)});
  m_panelDivider->setVisible(false);
  float reservedTop = 0;
  if (m_configErrorBanner->visible()) {
    m_configErrorHeading->setColor(colorForRole(ColorRole::Error));
    m_configErrorLabel->setColor(colorForRole(ColorRole::OnSurface));
    m_configErrorBanner->setStyle(
        {.fill = colorForRole(ColorRole::SurfaceContainer),
         .border = colorForRole(ColorRole::Error),
         .radius = 12,
         .borderWidth = 1}
    );
    const float bw = std::max(1.0F, std::min(sw - 24, 960.0F));
    m_configErrorHeading->setMaxWidth(bw - 28);
    m_configErrorHeading->measure(renderer);
    m_configErrorLabel->setMaxWidth(bw - 28);
    m_configErrorLabel->setMaxLines(2);
    m_configErrorLabel->measure(renderer);
    const float bh = 28 + m_configErrorHeading->height() + 8 + m_configErrorLabel->height();
    const float bx = (sw - bw) / 2, by = 12;
    m_configErrorBanner->setPosition(bx, by);
    m_configErrorBanner->setSize(bw, bh);
    m_configErrorHeading->setPosition(bx + 14, by + 14);
    m_configErrorLabel->setPosition(bx + 14, by + 22 + m_configErrorHeading->height());
    reservedTop = std::min(bh + 24, sh * .28F);
  }
  const auto& state = m_model.state();
  m_minimalLogin = minimalLogin();
  const bool busy = state.authenticating && !state.waitingForInput;
  const std::string name =
      state.passwordVisible ? std::string(m_model.displayName(state.selectedUser)) : "Choose an account";
  const std::string feedback = state.status == state.prompt ? "" : state.status;
  LoginPanelView view{
      .minimal = m_minimalLogin,
      .passwordStep = state.passwordVisible,
      .identitySearch = m_userMenuOpen,
      .blocked = m_auth.inputBlocked(m_viewId),
      .error = state.statusIsError,
      .busy = busy,
      .name = name.empty() ? state.username : name,
      .username = state.username,
      .prompt = state.prompt.empty() ? "Password" : state.prompt,
      .feedback = feedback
  };
  auto settings = m_layoutConfig;
  using Id = greeter::GreeterComponent;
  auto& login = settings.components[0];
  const float pw = std::min(
      std::clamp(login.width.value_or(m_minimalLogin ? 360.0F : 420.0F), 280.0F, 640.0F), std::max(1.0F, sw - 32)
  );
  const float availableH = std::max(1.0F, sh - reservedTop - 32);
  const float ph = m_loginComponent->measure(renderer, pw, availableH, view);
  m_clockTimeLabel->setTextAlign(TextAlign::Start);
  m_clockDateLabel->setTextAlign(TextAlign::Start);
  const float timeSize = sw < 1000 ? std::min(56.0F, m_clockTimeFontSize) : m_clockTimeFontSize;
  m_clockTimeLabel->setFontSize(timeSize);
  m_clockTimeLabel->measure(renderer);
  m_clockDateLabel->setFontSize(m_clockDateFontSize);
  m_clockDateLabel->measure(renderer);
  const float clockH = (m_clockTimeLabel->text().empty() ? 0 : m_clockTimeLabel->height() + 10)
      + (m_clockDateLabel->text().empty() ? 0 : m_clockDateLabel->height() + 24)
      + 18;
  auto layout = greeter::resolveGreeterLayout(
      settings, sw, sh - reservedTop, ph, m_minimalLogin, m_boundOutputName, m_outputIdentifier, clockH
  );
  for (auto& rect : layout.components)
    rect.y += reservedTop;
  const auto geometry = m_loginComponent->layout(renderer, layout[Id::Login], view);
  if (m_minimalLogin) {
    for (Node* n : std::array<Node*, 11>{
             m_userSelectBox, m_userSelectLabel, m_userSelectGlyph, m_userSelectArea, m_userMenuSearchField,
             m_userSecondaryLabel, m_sessionSelectBox, m_sessionSelectLabel, m_sessionSelectGlyph, m_sessionSelectArea,
             m_sessionCaptionLabel
         })
      n->setVisible(false);
    if (m_userMenuOpen || m_sessionMenuOpen)
      closeMenus();
  } else {
    layoutPanelUserSelector(
        geometry.identity.x, geometry.identity.y, geometry.identity.width, geometry.identity.height
    );
    layoutPanelSessionSelector(geometry.session.x, geometry.session.y, geometry.session.width, geometry.session.height);
  }
  layoutUserAvatar(renderer, geometry.avatarX, geometry.avatarY, geometry.avatarSize);
  if (m_userMenuOpen) {
    m_headerUserGlyph->setVisible(false);
    m_headerUserAvatar->setVisible(false);
  }
  m_loginButton->setEnabled(!view.blocked);
  m_passwordField->setEnabled(!view.blocked && state.passwordVisible);
  m_chromeComponent->layout(
      renderer, layout, *m_clockTimeLabel, *m_clockDateLabel, *m_brandLogo, m_brandLogoTexture.id != 0,
      m_clockTimeFontSize, m_clockDateFontSize, m_keyboardLayout, sw, sh, m_hasWallpaper
  );
  const auto& appearance = layout[Id::Appearance];
  m_schemeSelectBox->setVisible(appearance.visible);
  m_schemeSelectArea->setVisible(appearance.visible);
  m_schemeSelectIcon->setVisible(appearance.visible);
  m_schemeSelectLabel->setVisible(appearance.visible);
  if (appearance.visible) {
    const float x = appearance.x, y = appearance.y;
    m_schemeSelectBox->setPosition(x, y);
    m_schemeSelectBox->setSize(appearance.width, appearance.height);
    m_schemeSelectBox->setStyle(
        {.fill = clearColor(),
         .border = colorForRole(m_schemeSelectArea->focused() ? ColorRole::Secondary : ColorRole::OutlineVariant),
         .radius = 12,
         .borderWidth = m_schemeSelectArea->focused() ? 2.0F : 0.0F}
    );
    m_schemeSelectBox->layout(renderer);
    m_schemeSelectArea->setPosition(x, y);
    m_schemeSelectArea->setSize(appearance.width, appearance.height);
    m_schemeSelectIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    m_schemeSelectIcon->setPosition(x + 10, y + 10);
    m_schemeSelectIcon->setGlyphSize(17);
    m_schemeSelectIcon->measure(renderer);
    m_schemeSelectLabel->setText("Appearance");
    m_schemeSelectLabel->setFontSize(14);
    m_schemeSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
    m_schemeSelectLabel->measure(renderer);
    m_schemeSelectLabel->setPosition(x + 36, y + 10);
  } else if (m_schemeMenuOpen) {
    m_schemeMenuOpen = false;
    clearSchemeMenu();
  }
  const auto& powerRect = layout[Id::Power];
  m_powerMenuButton->setVisible(powerRect.visible);
  if (powerRect.visible) {
    m_powerMenuButton->setPosition(powerRect.x, powerRect.y);
    m_powerMenuButton->setSize(powerRect.width, powerRect.height);
    m_powerMenuButton->setControlHeight(powerRect.height);
    m_powerMenuButton->layout(renderer);
  }
  layoutPowerMenu(sw, sh);
  rebuildUserMenu();
  rebuildSessionMenu();
  rebuildSchemeMenu();
  rebuildFocusRing();
  applyMenuHighlight();
  if (!m_isKeyboardOwner)
    return;
  if (m_powerMenuOpen && m_focusIndex < 0) {
    m_inputDispatcher.setFocus(m_powerCancel->inputArea());
    syncFocusIndexFromFocused();
  }
  if (!m_initialFocusDone && !m_focusRing.empty()) {
    setFocusIndex(defaultFocusIndex());
    m_initialFocusDone = true;
  }
  if (m_userMenuSearchFocusPending && m_userMenuSearchField && m_userMenuSearchField->inputArea()) {
    m_inputDispatcher.setFocus(m_userMenuSearchField->inputArea());
    m_userMenuSearchFocusPending = false;
  } else if (!menuOpen() && !m_powerMenuOpen && state.passwordVisible && m_inputDispatcher.focusedArea() == nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
  }
}

void GreeterSurface::togglePowerMenu() {
  if (m_powerMenuOpen) {
    closePowerMenu();
    return;
  }
  closeMenus();
  m_powerMenuOpen = true;
  m_pendingPowerAction = 0;
  m_inputDispatcher.setFocus(nullptr);
  requestLayout();
  if (m_powerCancel->visible())
    m_inputDispatcher.setFocus(m_powerCancel->inputArea());
}
void GreeterSurface::closePowerMenu() {
  m_powerMenuOpen = false;
  m_pendingPowerAction = 0;
  requestLayout();
  m_inputDispatcher.setFocus(m_powerMenuButton->inputArea());
}
void GreeterSurface::requestPowerAction(int action) {
  m_powerMenuOpen = true;
  m_pendingPowerAction = action;
  requestLayout();
  m_inputDispatcher.setFocus(m_powerCancel->inputArea());
}
void GreeterSurface::layoutPowerMenu(float sw, float sh) {
  for (Node* node : std::array<Node*, 8>{
           m_powerPanel, m_powerTitle, m_suspendButton, m_rebootButton, m_shutdownButton, m_firmwareButton,
           m_powerCancel, m_powerConfirm
       })
    if (node)
      node->setVisible(false);
  if (!m_powerMenuOpen)
    return;
  const float w = std::min(320.0F, sw - 24),
              x = std::clamp(
                  m_powerMenuButton->x() - m_powerMenuButton->width() - w, 12.0F, std::max(12.0F, sw - w - 12)
              );
  const bool confirm = m_pendingPowerAction != 0;
  std::vector<Button*> actions;
  if (confirm)
    actions = {m_powerCancel, m_powerConfirm};
  else {
    if (power::hasSyncedAction("suspend"))
      actions.push_back(m_suspendButton);
    if (power::hasSyncedAction("reboot"))
      actions.push_back(m_rebootButton);
    if (power::hasSyncedAction("shutdown"))
      actions.push_back(m_shutdownButton);
    if (m_firmwareButton)
      actions.push_back(m_firmwareButton);
    actions.push_back(m_powerCancel);
  }
  const float rowH = std::min(40.0F, std::max(24.0F, (sh - 100) / static_cast<float>(actions.size())));
  const float h = 64 + rowH * static_cast<float>(actions.size());
  const float y = std::clamp(m_powerMenuButton->y() - h - 10, 12.0F, std::max(12.0F, sh - h - 12));
  m_powerPanel->setVisible(true);
  m_powerPanel->setPosition(x, y);
  m_powerPanel->setSize(w, h);
  m_powerPanel->setStyle(
      {.fill = colorForRole(ColorRole::SurfaceContainerHigh),
       .border = colorForRole(ColorRole::OutlineVariant),
       .radius = 18 * Style::cornerRadiusScale(),
       .borderWidth = 1}
  );
  const std::array<std::string_view, 5> labels{"Power", "Suspend?", "Restart?", "Shut down?", "Restart to firmware?"};
  m_powerTitle->setVisible(true);
  m_powerTitle->setText(labels[m_pendingPowerAction]);
  m_powerTitle->setFontSize(18);
  m_powerTitle->setColor(colorForRole(ColorRole::OnSurface));
  m_powerTitle->setMaxWidth(w - 32);
  m_powerTitle->measure(*m_renderer);
  m_powerTitle->setPosition(x + 16, y + 16);
  m_powerConfirm->setText(m_pendingPowerAction == 1 ? "Suspend" : m_pendingPowerAction == 3 ? "Shut down" : "Restart");
  m_powerConfirm->setVariant(m_pendingPowerAction == 3 ? ButtonVariant::Destructive : ButtonVariant::Primary);
  for (std::size_t i = 0; i < actions.size(); i++) {
    auto* button = actions[i];
    button->setVisible(true);
    button->setZIndex(102);
    button->setControlHeight(rowH);
    button->setPosition(x + 12, y + 52 + static_cast<float>(i) * rowH);
    button->setSize(w - 24, rowH);
    button->layout(*m_renderer);
  }
}

void GreeterSurface::refreshSelectionLabels() {
  if (m_userSelectLabel != nullptr) {
    const std::string userLabel = m_model.users().empty()
        ? "(none)"
        : m_model.users()[std::min(m_model.state().selectedUser, m_model.users().size() - 1)];
    m_userSelectLabel->setText(
        m_model.users().empty() ? "Choose an account" : m_model.displayName(m_model.state().selectedUser)
    );
    m_userSecondaryLabel->setText(userLabel);
    m_userSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_sessionSelectLabel != nullptr) {
    const std::string sessionLabel = m_model.sessions().empty()
        ? "/bin/sh"
        : m_model.sessions()[std::min(m_model.state().selectedSession, m_model.sessions().size() - 1)].name;
    m_sessionSelectLabel->setText(sessionLabel);
  }
  if (m_userSelectGlyph != nullptr) {
    m_userSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  syncPanelUserChrome();
  syncPanelSessionChrome();
  if (m_schemeSelectLabel != nullptr) {
    const std::string schemeLabel = m_model.schemeNames().empty()
        ? "Noctalia"
        : m_model.schemeNames()[std::min(m_model.state().selectedScheme, m_model.schemeNames().size() - 1)];
    m_schemeSelectLabel->setText(schemeLabel);
    m_schemeSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_schemeSelectIcon != nullptr) {
    m_schemeSelectIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  applySelectorBoxStyle(m_schemeSelectBox, m_schemeSelectArea);
}

bool GreeterSurface::isSyncedScheme(const std::size_t schemeIndex) const {
  return schemeIndex < m_model.schemeNames().size()
      && m_model.schemeNames()[schemeIndex] == greeter::appearance::kSyncedSchemeDisplayName;
}

std::optional<std::size_t> GreeterSurface::findSchemeIndex(const std::string_view name) const {
  for (std::size_t i = 0; i < m_model.schemeNames().size(); ++i) {
    if (m_model.schemeNames()[i] == name) {
      return i;
    }
  }
  return std::nullopt;
}

void GreeterSurface::setBoundOutputName(std::string outputName) {
  if (m_boundOutputName == outputName) {
    return;
  }
  m_boundOutputName = std::move(outputName);
  applyConfiguredWallpaper();
  requestLayout();
}

void GreeterSurface::setWallpaperSpanParams(const WallpaperSpanParams& params) {
  if (m_wallpaperSpanParams == params) {
    return;
  }
  m_wallpaperSpanParams = params;
  if (m_wallpaper != nullptr) {
    m_wallpaper->setSpan(params);
  }
  requestRedraw();
}

void GreeterSurface::clearWallpaperDisplay() {
  m_hasWallpaper = false;
  m_wallpaperPath.clear();
  m_wallpaperFillMode = WallpaperFillMode::Crop;
  m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  m_wallpaperBlur = 0.0f;
  m_wallpaperTint = 0.0f;
  m_wallpaperDirty = true;
}

void GreeterSurface::applyConfiguredWallpaper() {
  if (!m_wallpaperAppearance.has_value()) {
    clearWallpaperDisplay();
    return;
  }

  const auto wallpaper = m_wallpaperAppearance->wallpaperForOutput(m_boundOutputName);
  if (!wallpaper.has_value()) {
    clearWallpaperDisplay();
    return;
  }

  m_wallpaperPath = wallpaper->path;
  m_wallpaperFillMode = wallpaper->fillMode;
  m_wallpaperFillColor = wallpaper->fillColor;
  m_wallpaperBlur = wallpaper->blur;
  m_wallpaperTint = wallpaper->tint;
  m_hasWallpaper = !m_wallpaperPath.empty() || m_wallpaperFillColor.a > 0.0f;
  m_wallpaperDirty = true;
}

void GreeterSurface::applyScheme(const std::size_t schemeIndex) {
  if (schemeIndex >= m_model.schemeNames().size()) {
    return;
  }

  m_appliedScheme = schemeIndex;
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
    if (m_renderContext != nullptr && !m_syncedAppearance->fontFamily.empty()) {
      m_renderContext->setTextFontFamily(m_syncedAppearance->fontFamily);
    }
    applyConfiguredWallpaper();
    return;
  }

  if (const auto* builtinPalette = noctalia::theme::findBuiltinPalette(m_model.schemeNames()[schemeIndex])) {
    setPalette(builtinPalette->dark.palette);
  }
  Style::setCornerRadiusScale(1.0f);
  applyConfiguredWallpaper();
}

void GreeterSurface::layoutUserAvatar(Renderer& renderer, const float x, const float y, const float size) {
  const bool canShowAvatar = !m_model.users().empty()
      && m_model.state().selectedUser < m_model.users().size()
      && m_model.state().selectedUser < m_model.userIconPaths().size()
      && m_headerUserAvatar != nullptr
      && m_renderContext != nullptr;
  const std::string iconPath = canShowAvatar ? m_model.userIconPaths()[m_model.state().selectedUser] : std::string{};
  // size is logical; decode at buffer resolution so HiDPI outputs stay sharp.
  const int avatarPixelSize = canShowAvatar ? static_cast<int>(std::lround(size * renderer.renderScale())) : 0;

  if (canShowAvatar
      && !iconPath.empty()
      && (iconPath != m_loadedHeaderAvatarPath || avatarPixelSize != m_loadedHeaderAvatarPixelSize)) {
    if (m_headerAvatarTexture.id != 0) {
      m_renderContext->textureManager().unload(m_headerAvatarTexture);
      m_headerAvatarTexture = {};
    }
    m_loadedHeaderAvatarPath = iconPath;
    m_loadedHeaderAvatarPixelSize = avatarPixelSize;
    m_headerAvatarTexture = loadGreeterTexture(m_renderContext->textureManager(), iconPath, avatarPixelSize, true);
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
      m_headerUserGlyph->setPosition(
          x + std::round((size - m_headerUserGlyph->width()) / 2),
          y + std::round((size - m_headerUserGlyph->height()) / 2)
      );
    }
    return;
  }

  if (m_headerUserGlyph != nullptr) {
    m_headerUserGlyph->setVisible(false);
  }

  m_headerUserAvatar->setTextureId(m_headerAvatarTexture.id);
  m_headerUserAvatar->setTextureSize(m_headerAvatarTexture.width, m_headerAvatarTexture.height);
  m_headerUserAvatar->setTint({1.0f, 1.0f, 1.0f, 1.0f});
  m_headerUserAvatar->setRadius(size * 0.5f);
  m_headerUserAvatar->setBorder(colorForRole(ColorRole::Primary), Style::borderWidth * kHeaderAvatarBorderScale);
  m_headerUserAvatar->setFitMode(ImageFitMode::Cover);
  m_headerUserAvatar->setSize(size, size);
  m_headerUserAvatar->setPosition(x, y);
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

  const auto useFillColor = [this]() {
    if (m_wallpaperFillColor.a <= 0.0f) {
      return false;
    }
    m_wallpaper->setSources(
        WallpaperSourceKind::Color, {}, m_wallpaperFillColor, WallpaperSourceKind::Color, {}, m_wallpaperFillColor,
        0.0f, 0.0f, 0.0f, 0.0f
    );
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
    return true;
  };

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
      m_wallpaperTexture = loadGreeterTexture(m_renderContext->textureManager(), m_wallpaperPath, 0, true);
      if (m_wallpaperTexture.id != 0) {
        m_wallpaper->setTextures(
            m_wallpaperTexture.id, {}, static_cast<float>(m_wallpaperTexture.width),
            static_cast<float>(m_wallpaperTexture.height), 0.0f, 0.0f
        );
        m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
        m_wallpaper->setFillMode(m_wallpaperFillMode);
        m_wallpaper->setFillColor(m_wallpaperFillColor);
      } else {
        if (!useFillColor()) {
          m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
          m_hasWallpaper = false;
        }
      }
    } else {
      if (!useFillColor()) {
        m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
        m_hasWallpaper = false;
      }
    }
  } else if (!useFillColor()) {
    m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
  }

  m_wallpaperDirty = false;
}

void GreeterSurface::loadPreferences() {
  const auto prefs = greeter::loadGreeterPreferences();
  m_layoutConfig = greeter::effectiveGreeterLayout(prefs);
  m_clockEnabled = m_layoutConfig.components[1].visible.value_or(prefs.clockEnabled);
  m_clockTimeFormat = prefs.clockTimeFormat;
  m_clockDateFormat = prefs.clockDateFormat;
  m_clockTimeFontSize = prefs.clockTimeFontSize;
  m_clockDateFontSize = prefs.clockDateFontSize;
}

void GreeterSurface::openUserMenu() {
  if (m_userMenuOpen || !showsUserDropdown()) {
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
  if (m_minimalLogin)
    return;
  m_sessionMenuOpen = !m_sessionMenuOpen;
  if (m_sessionMenuOpen) {
    m_userMenuOpen = false;
    m_schemeMenuOpen = false;
    clearUserMenu();
    clearSchemeMenu();
  }
  m_menuHighlight = -1;
  m_sessionMenuScroll.offset = 0;
  m_menuScrollRemainder = 0.0f;
  requestLayout();
}

void GreeterSurface::toggleSchemeMenu() {
  // Don't toggle the menu if the selector is hidden
  if (!m_schemeSelectArea->visible()) {
    m_schemeMenuOpen = false;
    requestLayout();
    return;
  }
  m_schemeMenuOpen = !m_schemeMenuOpen;
  if (m_schemeMenuOpen) {
    m_userMenuOpen = false;
    m_sessionMenuOpen = false;
    clearUserMenu();
    clearSessionMenu();
  }
  m_menuHighlight = -1;
  m_schemeMenuScroll.offset = 0;
  m_menuScrollRemainder = 0.0f;
  requestLayout();
}

void GreeterSurface::closeMenus() {
  m_userMenuOpen = false;
  m_sessionMenuOpen = false;
  m_schemeMenuOpen = false;
  m_powerMenuOpen = false;
  m_userMenuSearchQuery.clear();
  m_menuHighlight = -1;
  clearUserMenu();
  clearSessionMenu();
  clearSchemeMenu();
}

InputArea* GreeterSurface::menuReturnFocusTarget() const {
  // The session and scheme selectors are a detour from the only control that
  // matters on the password step, and the pointer path cannot infer this from the
  // previous focus: a click focuses the selector before its handler runs, so
  // "where focus was" is already the selector itself. Naming the password field
  // outright keeps keyboard, focus-ring and pointer opens behaving alike.
  if (!m_model.state().passwordVisible || m_passwordField == nullptr) {
    return nullptr;
  }
  return m_passwordField->inputArea();
}

void GreeterSurface::closeMenusAndRestoreFocus() {
  InputArea* owner = m_userMenuOpen ? m_userSelectArea
      : m_sessionMenuOpen           ? m_sessionSelectArea
      : m_schemeMenuOpen            ? m_schemeSelectArea
                                    : nullptr;
  InputArea* target = owner;
  // Browsing a menu preserves an already selected account and its form.
  if (InputArea* passwordArea = menuReturnFocusTarget(); passwordArea != nullptr) {
    target = passwordArea;
  }
  closeMenus();
  if (target != nullptr) {
    m_inputDispatcher.setFocus(target);
  }
  requestLayout();
}

void GreeterSurface::selectSession(std::size_t index) {
  if (index >= m_model.sessions().size()) {
    return;
  }
  m_model.selectSession(m_viewId, index);
  refreshSelectionLabels();
  m_sessionMenuOpen = false;
  m_menuHighlight = -1;
  InputArea* sessionFocus = menuReturnFocusTarget();
  if (sessionFocus == nullptr) {
    sessionFocus = m_sessionSelectArea;
  }
  if (sessionFocus != nullptr) {
    m_inputDispatcher.setFocus(sessionFocus);
  }
  commitImmediateFrame(true);
}

void GreeterSurface::selectScheme(std::size_t index) {
  if (index >= m_model.schemeNames().size()) {
    return;
  }
  m_model.selectScheme(m_viewId, index);
  refreshSelectionLabels();
  m_schemeMenuOpen = false;
  m_menuHighlight = -1;
  InputArea* schemeFocus = menuReturnFocusTarget();
  if (schemeFocus == nullptr) {
    schemeFocus = m_schemeSelectArea;
  }
  if (schemeFocus != nullptr) {
    m_inputDispatcher.setFocus(schemeFocus);
  }
  commitImmediateFrame(true);
}

void GreeterSurface::runBackAction() {
  if (!m_auth.showUserPicker(m_viewId)) {
    return;
  }
  if (m_userSelectArea != nullptr) {
    m_inputDispatcher.setFocus(m_userSelectArea);
  }
  commitImmediateFrame(true);
}

bool GreeterSurface::menuOpen() const noexcept { return m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen; }

void GreeterSurface::rebuildFocusRing() {
  InputArea* previouslyFocused = m_inputDispatcher.focusedArea();
  m_focusRing.clear();
  if (m_powerMenuOpen) {
    for (Button* b :
         {m_suspendButton, m_rebootButton, m_shutdownButton, m_firmwareButton, m_powerCancel, m_powerConfirm})
      if (b && b->visible())
        m_focusRing.push_back({b->inputArea(), [b] { b->inputArea()->dispatchKey(XKB_KEY_Return, 0, 0, true); }});
    m_focusIndex = -1;
    for (std::size_t i = 0; i < m_focusRing.size(); i++)
      if (m_focusRing[i].area == previouslyFocused)
        m_focusIndex = static_cast<std::ptrdiff_t>(i);
    return;
  }

  if (m_model.state().passwordVisible) {
    if (m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
      m_focusRing.push_back({m_passwordField->inputArea(), {}});
    }
    if (m_loginButton != nullptr && m_loginButton->inputArea() != nullptr) {
      m_focusRing.push_back({m_loginButton->inputArea(), [this]() { tryAuthenticate(); }});
    }
  } else if (showsUserDropdown() && m_userSelectArea != nullptr) {
    m_focusRing.push_back({m_userSelectArea, [this]() {
                             if (!m_model.state().passwordVisible
                                 && m_model.state().selectedUser < m_model.users().size()) {
                               enterPasswordStep(m_model.state().selectedUser);
                             }
                           }});
  }

  if (m_sessionSelectArea != nullptr && m_sessionSelectArea->visible()) {
    m_focusRing.push_back({m_sessionSelectArea, [this]() { toggleSessionMenu(); }});
  }
  if (m_userSelectArea && m_userSelectArea->visible() && m_model.state().passwordVisible)
    m_focusRing.push_back({m_userSelectArea, [this] { openUserMenu(); }});
  if (m_powerMenuButton && m_powerMenuButton->visible())
    m_focusRing.push_back({m_powerMenuButton->inputArea(), [this] { togglePowerMenu(); }});
  if (m_schemeSelectArea != nullptr && m_schemeSelectArea->visible()) {
    m_focusRing.push_back({m_schemeSelectArea, [this]() { toggleSchemeMenu(); }});
  }

  if (m_firmwareButton != nullptr && m_firmwareButton->inputArea() != nullptr && m_firmwareButton->visible()) {
    m_focusRing.push_back({m_firmwareButton->inputArea(), [this]() { requestPowerAction(4); }});
  }
  if (m_rebootButton != nullptr && m_rebootButton->inputArea() != nullptr && m_rebootButton->visible()) {
    m_focusRing.push_back({m_rebootButton->inputArea(), [this]() { requestPowerAction(2); }});
  }
  if (m_shutdownButton != nullptr && m_shutdownButton->inputArea() != nullptr && m_shutdownButton->visible()) {
    m_focusRing.push_back({m_shutdownButton->inputArea(), [this]() { requestPowerAction(3); }});
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

void GreeterSurface::applySelectorBoxStyle(Box* box, const InputArea* area) {
  if (box == nullptr) {
    return;
  }
  const bool menuOpen = (box == m_schemeSelectBox && m_schemeMenuOpen) || (box == m_userSelectBox && m_userMenuOpen);
  const bool active = menuOpen || (area != nullptr && area->focused());
  box->setStyle(
      RoundedRectStyle{
          .fill = active ? colorForRole(ColorRole::Surface) : colorForRole(ColorRole::SurfaceContainer),
          .border = active ? colorForRole(ColorRole::Primary) : colorForRole(ColorRole::OutlineVariant),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = active ? std::max(Style::borderWidth, Style::borderWidth * 2.0f) : Style::borderWidth,
      }
  );
}

void GreeterSurface::syncPanelUserChrome() {
  if (!m_userSelectBox || !m_userSelectArea)
    return;
  const bool active = m_userMenuOpen || m_userSelectArea->focused();
  m_userSelectBox->setStyle(
      {.fill = colorForRole(ColorRole::Surface),
       .border = colorForRole(active ? ColorRole::Primary : ColorRole::OutlineVariant),
       .radius = 12 * Style::cornerRadiusScale(),
       .borderWidth = active ? 2.0F : 1.0F}
  );
  m_userSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  m_userSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  if (m_userSecondaryLabel)
    m_userSecondaryLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  if (m_userMenuSearchField)
    m_userMenuSearchField->setActiveChrome(m_userMenuOpen);
}
void GreeterSurface::layoutPanelUserSelector(float x, float y, float w, float h) {
  if (!m_renderer)
    return;
  syncPanelUserChrome();
  m_userSelectBox->setZIndex(5);
  m_userSelectBox->setVisible(true);
  m_userSelectBox->setPosition(x, y);
  m_userSelectBox->setSize(w, h);
  m_userSelectBox->layout(*m_renderer);
  m_userSelectLabel->setVisible(!m_userMenuOpen);
  m_userSecondaryLabel->setVisible(!m_userMenuOpen);
  m_userSelectArea->setVisible(!m_userMenuOpen);
  m_userSelectArea->setPosition(x, y);
  m_userSelectArea->setSize(w, h);
  m_userMenuSearchField->setVisible(m_userMenuOpen);
  if (m_userMenuOpen)
    layoutUserMenuSearchField(x + 13, y, w - 48, h);
  else {
    m_userSelectLabel->setFontSize(16);
    m_userSelectLabel->setFontWeight(FontWeight::Medium);
    m_userSelectLabel->setMaxWidth(std::max(1.0F, w - 98));
    m_userSelectLabel->measure(*m_renderer);
    m_userSelectLabel->setPosition(x + 64, y + (h - m_userSelectLabel->height() - 18) / 2);
    m_userSecondaryLabel->setFontSize(12);
    m_userSecondaryLabel->setMaxWidth(std::max(1.0F, w - 98));
    m_userSecondaryLabel->measure(*m_renderer);
    m_userSecondaryLabel->setPosition(
        x + 64, y + (h - m_userSelectLabel->height() - 18) / 2 + m_userSelectLabel->height() + 2
    );
  }
  m_userSelectGlyph->setVisible(true);
  m_userSelectGlyph->setGlyphSize(16);
  m_userSelectGlyph->measure(*m_renderer);
  m_userSelectGlyph->setPosition(x + w - 28, y + (h - m_userSelectGlyph->height()) / 2);
}
void GreeterSurface::syncPanelSessionChrome() {
  if (!m_sessionSelectBox || !m_sessionSelectArea)
    return;
  const bool active = m_sessionMenuOpen || m_sessionSelectArea->focused();
  m_sessionSelectBox->setStyle(
      {.fill = clearColor(),
       .border = colorForRole(ColorRole::Primary),
       .radius = 10 * Style::cornerRadiusScale(),
       .borderWidth = active ? 2.0F : 0.0F}
  );
  m_sessionSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  m_sessionSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
}
void GreeterSurface::layoutPanelSessionSelector(float x, float y, float w, float h) {
  if (!m_renderer)
    return;
  syncPanelSessionChrome();
  m_sessionSelectBox->setVisible(true);
  m_sessionSelectBox->setPosition(x, y);
  m_sessionSelectBox->setSize(w, h);
  m_sessionSelectBox->layout(*m_renderer);
  m_panelDivider->setZIndex(6);
  m_panelDivider->setVisible(true);
  m_panelDivider->setPosition(x, y - 8);
  m_panelDivider->setSize(w, 1);
  m_panelDivider->setStyle({.fill = colorForRole(ColorRole::OutlineVariant, .6F)});
  m_sessionCaptionLabel->setVisible(true);
  m_sessionCaptionLabel->setText("Session");
  m_sessionCaptionLabel->setFontSize(13);
  m_sessionCaptionLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_sessionCaptionLabel->setMaxWidth(w * .3F);
  m_sessionCaptionLabel->measure(*m_renderer);
  m_sessionCaptionLabel->setPosition(x, y + (h - m_sessionCaptionLabel->height()) / 2);
  m_sessionSelectGlyph->setVisible(true);
  m_sessionSelectGlyph->setGlyphSize(16);
  m_sessionSelectGlyph->measure(*m_renderer);
  m_sessionSelectGlyph->setPosition(x + w - 16, y + (h - m_sessionSelectGlyph->height()) / 2);
  m_sessionSelectLabel->setVisible(true);
  m_sessionSelectLabel->setFontSize(13);
  m_sessionSelectLabel->setMaxWidth(std::max(1.0F, w * .65F - 28));
  m_sessionSelectLabel->measure(*m_renderer);
  m_sessionSelectLabel->setPosition(
      x + w - 28 - m_sessionSelectLabel->width(), y + (h - m_sessionSelectLabel->height()) / 2
  );
  m_sessionSelectArea->setVisible(true);
  m_sessionSelectArea->setPosition(x, y);
  m_sessionSelectArea->setSize(w, h);
}

std::ptrdiff_t GreeterSurface::defaultFocusIndex() const {
  if (m_focusRing.empty()) {
    return 0;
  }

  if (m_model.state().passwordVisible && m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
    for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
      if (m_focusRing[i].area == m_passwordField->inputArea()) {
        return static_cast<std::ptrdiff_t>(i);
      }
    }
  }

  if (!m_model.state().passwordVisible && showsUserDropdown() && m_userSelectArea != nullptr) {
    for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
      if (m_focusRing[i].area == m_userSelectArea) {
        return static_cast<std::ptrdiff_t>(i);
      }
    }
  }

  return 0;
}

void GreeterSurface::syncFocusIndexFromFocused() {
  InputArea* focused = m_inputDispatcher.focusedArea();
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
    selected = m_model.state().selectedSession;
  } else if (m_schemeMenuOpen) {
    rows = &m_schemeMenuRows;
    labels = &m_schemeMenuLabels;
    selected = m_model.state().selectedScheme;
  } else if (m_userMenuOpen) {
    rows = &m_userMenuRows;
    labels = &m_userMenuLabels;
    if (m_model.state().selectedUser < m_model.users().size()) {
      for (std::size_t i = 0; i < m_userMenuFilteredIndices.size(); ++i) {
        if (m_userMenuFilteredIndices[i] == m_model.state().selectedUser) {
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
  const std::size_t totalCount = m_userMenuOpen ? m_userMenuFilteredIndices.size()
      : m_sessionMenuOpen                       ? m_model.sessions().size()
                                                : m_model.schemeNames().size();
  if (m_userMenuOpen) {
    const std::size_t matchCount = m_userMenuFilteredIndices.size();
    if (matchCount == 0) {
      m_menuHighlight = -1;
      return;
    }
    if (m_menuHighlight < 0 || static_cast<std::size_t>(m_menuHighlight) >= matchCount) {
      m_menuHighlight = static_cast<std::ptrdiff_t>(std::min(selected, matchCount - 1));
    }
  } else if (m_menuHighlight < 0 || static_cast<std::size_t>(m_menuHighlight) >= totalCount) {
    m_menuHighlight = static_cast<std::ptrdiff_t>(std::min(selected, totalCount - 1));
  }

  for (std::ptrdiff_t i = 0; i < count; ++i) {
    const std::size_t index = static_cast<std::size_t>(i);
    Box* row = (*rows)[index];
    if (row == nullptr) {
      continue;
    }

    const MenuScroll* scroll = activeMenuScroll();
    const std::size_t filteredIndex = (m_userMenuOpen          ? m_userMenuScrollOffset
                                           : scroll != nullptr ? scroll->offset
                                                               : 0)
        + index;
    const bool highlighted = filteredIndex == static_cast<std::size_t>(m_menuHighlight);
    const bool isSelected = m_userMenuOpen
        ? (filteredIndex < m_userMenuFilteredIndices.size()
           && m_userMenuFilteredIndices[filteredIndex] == m_model.state().selectedUser)
        : (filteredIndex == selected);
    const float borderWidth = Style::borderWidth;
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

    Color rowFill = highlighted ? colorForRole(ColorRole::Secondary) : colorForRole(ColorRole::SurfaceContainer);
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
        rowFill = colorForRole(ColorRole::SurfaceContainer, 0.01f);
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

  MenuScroll* scroll = activeMenuScroll();
  if (scroll == nullptr || scroll->visibleCount == 0) {
    return;
  }
  const std::ptrdiff_t count =
      static_cast<std::ptrdiff_t>(m_sessionMenuOpen ? m_model.sessions().size() : m_model.schemeNames().size());
  if (m_menuHighlight < 0) {
    m_menuHighlight = 0;
  }
  m_menuHighlight = (((m_menuHighlight + delta) % count) + count) % count;
  const std::size_t highlight = static_cast<std::size_t>(m_menuHighlight);
  const std::size_t previousScroll = scroll->offset;
  if (highlight < scroll->offset) {
    scroll->offset = highlight;
  } else if (highlight >= scroll->offset + scroll->visibleCount) {
    scroll->offset = highlight + 1 - scroll->visibleCount;
  }
  if (scroll->offset != previousScroll) {
    requestLayout();
    return;
  }
  applyMenuHighlight();
  requestRedraw();
}

void GreeterSurface::ensureUserMenuHighlightVisible() {
  if (!m_userMenuOpen || m_userMenuFilteredIndices.empty()) {
    return;
  }

  const std::size_t matchCount = m_userMenuFilteredIndices.size();
  const std::size_t visibleCount = std::min(matchCount, m_userMenuVisibleRows);
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
  const std::size_t visibleCount = std::min(matchCount, m_userMenuVisibleRows);
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

GreeterSurface::MenuScroll* GreeterSurface::activeMenuScroll() {
  return m_sessionMenuOpen ? &m_sessionMenuScroll : m_schemeMenuOpen ? &m_schemeMenuScroll : nullptr;
}

void GreeterSurface::scrollSelectorMenu(int delta) {
  MenuScroll* scroll = activeMenuScroll();
  if (scroll == nullptr || scroll->visibleCount == 0 || delta == 0) {
    return;
  }
  const std::size_t count = m_sessionMenuOpen ? m_model.sessions().size() : m_model.schemeNames().size();
  const std::ptrdiff_t maxOffset = static_cast<std::ptrdiff_t>(count - scroll->visibleCount);
  const std::size_t offset = static_cast<std::size_t>(
      std::clamp(static_cast<std::ptrdiff_t>(scroll->offset) + delta, std::ptrdiff_t{0}, maxOffset)
  );
  if (offset != scroll->offset) {
    scroll->offset = offset;
    requestLayout();
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
  if (m_powerMenuOpen) {
    if (KeySymbol::isEscape(sym))
      closePowerMenu();
    else if (KeySymbol::isTab(sym) || KeySymbol::isUp(sym) || KeySymbol::isDown(sym))
      moveFocus((modifiers & KeyMod::Shift) || KeySymbol::isUp(sym) ? -1 : 1);
    else if (KeySymbol::isEnter(sym) || sym == XKB_KEY_space)
      activateFocused();
    return true;
  }
  if (m_auth.authInProgress(m_viewId) && !KeySymbol::isEscape(sym)) {
    return false;
  }
  const bool shift = (modifiers & KeyMod::Shift) != 0;
  const bool backward = shift || sym == XKB_KEY_ISO_Left_Tab;

  // F3 / F7 jump straight to the session / color-scheme menus from anywhere
  // (even from inside the password field or with another menu open).
  if ((sym == XKB_KEY_F3)) {
    if (m_minimalLogin)
      return true;
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
  if ((sym == XKB_KEY_F7)) {
    // Don't respond if the scheme selector is hidden
    if (!m_schemeSelectArea->visible()) {
      return true;
    }
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
      const bool searchFocused = searchArea != nullptr && m_inputDispatcher.focusedArea() == searchArea;

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
    if (m_model.state().passwordVisible) {
      runBackAction();
      return true;
    }
    return false;
  }

  // Let the password field keep all of its editing/submit keys.
  InputArea* focused = m_inputDispatcher.focusedArea();
  if (m_passwordField != nullptr && focused == m_passwordField->inputArea()) {
    return false;
  }

  if (!menuOpen()
      && !m_model.state().passwordVisible
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
    if (m_inputDispatcher.focusedArea() == nullptr && !m_focusRing.empty()) {
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
  if (!m_sessionMenuOpen && m_sessionMenuScroll.bar != nullptr) {

    (void)m_root.removeChild(m_sessionMenuScroll.bar);
    m_sessionMenuScroll = {};
  }
}

void GreeterSurface::clearSchemeMenu() {
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
  if (!m_schemeMenuOpen && m_schemeMenuScroll.bar != nullptr) {

    (void)m_root.removeChild(m_schemeMenuScroll.bar);
    m_schemeMenuScroll = {};
  }
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
  m_userMenuSearchField->layout(*m_renderer);
}

void GreeterSurface::refreshUserMenuRows() {
  if (!m_userMenuOpen || m_model.users().empty() || m_userSelectBox == nullptr || m_renderContext == nullptr) {
    return;
  }

  auto* renderer = m_renderer;
  m_userMenuFilteredIndices = filterUserIndices(m_model.users(), m_userMenuSearchQuery);

  const float anchorX = m_userSelectBox->x();
  const float anchorW = m_userSelectBox->width();
  const float rowH = GreeterMetrics::accountRowHeight;
  const float borderWidth = Style::borderWidth;
  const float panelRadius = Style::scaledRadiusLg();
  const float rowRadius = Style::scaledRadiusSm();
  const float rowIconReserve = Style::spaceMd + Style::fontSizeBody + kUserMenuRowIconGap;

  float contentW = anchorW;
  for (const std::size_t userIndex : m_userMenuFilteredIndices) {
    const float textW = renderer->measureText(m_model.users()[userIndex], Style::fontSizeBody).width;
    contentW = std::max(contentW, rowIconReserve + textW + 2.0f * kUserMenuPadding);
  }
  const float screenW = m_root.width();
  const float maxW = std::max(anchorW, screenW - 2.0f * Style::spaceLg);
  const float panelW = std::min(contentW, maxW);
  const float panelX = std::clamp(anchorX, Style::spaceLg, std::max(Style::spaceLg, screenW - Style::spaceLg - panelW));
  const float minY = 12;
  const float maxY = m_root.height() - 12;
  const float below = maxY - (m_userSelectBox->y() + m_userSelectBox->height() + 4);
  const float above = m_userSelectBox->y() - 4 - minY;
  const bool openAbove = above > below;
  const float space = std::max(0.0F, openAbove ? above : below);
  m_userMenuVisibleRows = std::clamp(
      static_cast<std::size_t>(std::max(
          1.0F, std::floor((space - 2 * (kUserMenuPadding + borderWidth) + kUserMenuRowGap) / (rowH + kUserMenuRowGap))
      )),
      std::size_t{1}, kUserDropdownMaxVisibleRows
  );
  const float rows =
      static_cast<float>(std::max(std::size_t{1}, std::min(m_userMenuFilteredIndices.size(), m_userMenuVisibleRows)));
  const float estimatedH = 2 * (kUserMenuPadding + borderWidth) + rows * rowH + (rows - 1) * kUserMenuRowGap;
  const float panelY = openAbove ? std::max(minY, m_userSelectBox->y() - 4 - estimatedH)
                                 : m_userSelectBox->y() + m_userSelectBox->height() + 4;
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
            .border = colorForRole(ColorRole::OutlineVariant),
            .fillMode = FillMode::Solid,
            .radius = panelRadius,
            .softness = 1.0f,
            .borderWidth = borderWidth,
        }
    );

    auto empty = std::make_unique<Label>();
    m_userMenuEmptyLabel = empty.get();
    m_userMenuEmptyLabel->setText("No users found");
    m_userMenuEmptyLabel->setFontSize(Style::fontSizeBody);
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
    if (m_model.state().selectedUser < m_model.users().size()) {
      for (std::size_t i = 0; i < matchCount; ++i) {
        if (m_userMenuFilteredIndices[i] == m_model.state().selectedUser) {
          m_menuHighlight = static_cast<std::ptrdiff_t>(i);
          break;
        }
      }
    }
  }
  ensureUserMenuHighlightVisible();

  const std::size_t visibleCount = std::min(m_userMenuVisibleRows, matchCount - m_userMenuScrollOffset);
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
          .border = colorForRole(ColorRole::OutlineVariant),
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
            .fill = colorForRole(ColorRole::SurfaceContainer, 0.01f),
            .fillMode = FillMode::Solid,
            .radius = rowRadius,
        }
    );
    m_root.addChild(std::move(row));
    m_userMenuRows.push_back(rowPtr);

    auto icon = std::make_unique<Glyph>();
    auto* iconPtr = icon.get();
    iconPtr->setGlyph("user");
    iconPtr->setGlyphSize(Style::fontSizeBody);
    iconPtr->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    iconPtr->setHitTestVisible(false);
    iconPtr->setZIndex(52);
    m_root.addChild(std::move(icon));
    (void)iconPtr->measure(*renderer);
    const float iconW = iconPtr->width();
    const float iconY = rowY + std::round((rowH - iconPtr->height()) / 2);
    iconPtr->setPosition(innerX + Style::spaceSm, iconY);
    m_userMenuRowIcons.push_back(iconPtr);

    auto label = std::make_unique<Label>();
    auto* labelPtr = label.get();
    labelPtr->setText(m_model.users()[userIndex]);
    labelPtr->setFontSize(Style::fontSizeBody);
    labelPtr->setColor(colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(52);
    m_root.addChild(std::move(label));
    labelPtr->measure(*renderer);
    labelPtr->setPosition(
        innerX + Style::spaceSm + iconW + kUserMenuRowIconGap, rowY + std::round((rowH - labelPtr->height()) * 0.5f)
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
  if (!m_userMenuOpen || m_model.users().empty()) {
    clearUserMenu();
    return;
  }

  refreshUserMenuRows();
}

void GreeterSurface::buildMenu(
    const std::vector<std::string>& names, std::size_t selected, Box* anchor, bool upward, bool rightAlign, int zBase,
    Box*& panelOut, std::vector<Box*>& rows, std::vector<Label*>& labels, std::vector<InputArea*>& areas,
    MenuScroll& scroll, std::function<void(std::size_t)> onSelect
) {
  const std::size_t count = names.size();
  if (anchor == nullptr || count == 0) {
    return;
  }

  const float rowH = GreeterMetrics::selectorRowHeight;
  const float borderWidth = Style::borderWidth;
  const float margin = Style::spaceLg;
  const float screenW = m_root.width();
  const float screenH = m_root.height();
  const float top = margin;
  const float bottom = screenH - margin;
  const float above = std::max(0.0f, anchor->y() - Style::spaceXs - top);
  const float below = std::max(0.0f, bottom - anchor->y() - anchor->height() - Style::spaceXs);
  const float desiredH = rowH * static_cast<float>(std::min(count, kSelectorMenuMaxVisibleRows)) + 2.0f * borderWidth;
  if (desiredH > (upward ? above : below) && (upward ? below : above) > (upward ? above : below)) {
    upward = !upward;
  }
  const float availableH = upward ? above : below;
  const std::size_t availableRows =
      static_cast<std::size_t>(std::max(1.0f, std::floor((availableH - 2.0f * borderWidth) / rowH)));
  scroll.visibleCount = std::min({count, kSelectorMenuMaxVisibleRows, availableRows});
  const std::size_t maxOffset = count - scroll.visibleCount;
  if (m_menuHighlight < 0) {
    m_menuHighlight = static_cast<std::ptrdiff_t>(std::min(selected, count - 1));
    const std::size_t highlight = static_cast<std::size_t>(m_menuHighlight);
    scroll.offset = highlight >= scroll.visibleCount ? highlight + 1 - scroll.visibleCount : 0;
  }
  scroll.offset = std::min(scroll.offset, maxOffset);

  const float gutter = maxOffset > 0 ? (GreeterMetrics::scrollbarWidth + Style::scrollbarGap) : 0.0f;
  float contentW = anchor->width();
  for (const auto& name : names) {
    const float textW = m_renderer->measureText(name, Style::fontSizeBody).width;
    contentW = std::max(contentW, textW + 2.0f * Style::spaceMd + gutter + 2.0f * borderWidth);
  }
  const float w = std::min(contentW, std::max(1.0f, screenW - 2.0f * margin));
  const float minX = margin;
  const float maxX = std::max(minX, screenW - margin - w);
  const float x = std::clamp(rightAlign ? anchor->x() + anchor->width() - w : anchor->x(), minX, maxX);
  const float listH = rowH * static_cast<float>(scroll.visibleCount);
  const float panelH = listH + 2.0f * borderWidth;
  const float y = std::clamp(
      upward ? anchor->y() - panelH - Style::spaceXs : anchor->y() + anchor->height() + Style::spaceXs, top,
      std::max(top, bottom - panelH)
  );

  auto panel = std::make_unique<Box>();
  panelOut = panel.get();
  panelOut->setZIndex(zBase);
  panelOut->setHitTestVisible(false);
  m_root.addChild(std::move(panel));
  panelOut->setPosition(x, y);
  panelOut->setSize(w, panelH);
  panelOut->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::SurfaceContainer),
          .border = colorForRole(ColorRole::OutlineVariant),
          .fillMode = FillMode::Solid,
          .radius = Style::scaledRadiusLg(),
          .softness = 1.0f,
          .borderWidth = borderWidth,
      }
  );

  const float panelRadius = Style::scaledRadiusLg();
  for (std::size_t visibleIndex = 0; visibleIndex < scroll.visibleCount; ++visibleIndex) {
    const std::size_t i = scroll.offset + visibleIndex;
    const float rowX = x + borderWidth;
    const float rowY = y + borderWidth + rowH * static_cast<float>(visibleIndex);
    const float rowW = w - 2.0f * borderWidth - gutter;

    auto row = std::make_unique<Box>();
    auto* rowPtr = row.get();
    rowPtr->setZIndex(zBase + 1);
    rowPtr->setPosition(rowX, rowY);
    rowPtr->setSize(rowW, rowH);

    Radii rowRadius(0.0f);
    if (scroll.visibleCount == 1) {
      rowRadius = Radii(panelRadius - borderWidth);
    } else if (visibleIndex == 0) {
      rowRadius = Radii(panelRadius - borderWidth, panelRadius - borderWidth, 0.0f, 0.0f);
    } else if (visibleIndex == scroll.visibleCount - 1) {
      rowRadius = Radii(0.0f, 0.0f, panelRadius - borderWidth, panelRadius - borderWidth);
    }
    rowPtr->setStyle(
        RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceContainer),
            .fillMode = FillMode::Solid,
            .radius = rowRadius,
        }
    );
    m_root.addChild(std::move(row));
    rows.push_back(rowPtr);

    auto label = std::make_unique<Label>();
    auto* labelPtr = label.get();
    labelPtr->setText(names[i]);
    labelPtr->setFontSize(Style::fontSizeBody);
    labelPtr->setColor(colorForRole(ColorRole::OnSurface));
    labelPtr->setMaxLines(1);
    labelPtr->setMaxWidth(std::max(1.0f, rowW - 2.0f * Style::spaceMd));
    labelPtr->setZIndex(zBase + 2);
    m_root.addChild(std::move(label));
    labelPtr->measure(*m_renderer);
    labelPtr->setPosition(rowX + Style::spaceMd, rowY + std::round((rowH - labelPtr->height()) * 0.5f));
    labels.push_back(labelPtr);

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
      if (data.button == BTN_LEFT) {
        onSelect(i);
      }
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(rowX, rowY);
    areaPtr->setSize(rowW, rowH);
    areas.push_back(areaPtr);
  }

  if (maxOffset > 0 && scroll.bar == nullptr) {
    auto bar = std::make_unique<Scrollbar>();
    scroll.bar = bar.get();
    scroll.bar->setZIndex(zBase + 4);
    m_root.addChild(std::move(bar));
  }
  if (scroll.bar != nullptr) {

    scroll.bar->setTrackInset(std::min(panelRadius, listH * 0.25f));
    scroll.bar->setPosition(x + w - borderWidth - GreeterMetrics::scrollbarWidth, y + borderWidth);
    scroll.bar->setSize(GreeterMetrics::scrollbarWidth, listH);
    scroll.bar->setOnScrollChanged([this, &scroll, rowH, maxOffset](float offset) {
      const std::size_t rowOffset = std::min(maxOffset, static_cast<std::size_t>(std::round(offset / rowH)));
      if (rowOffset != scroll.offset) {
        scroll.offset = rowOffset;
        requestLayout();
      }
    });
    scroll.bar->update(listH, rowH * static_cast<float>(count), rowH * static_cast<float>(scroll.offset));
  }
}

void GreeterSurface::rebuildSessionMenu() {
  clearSessionMenu();
  if (!m_sessionMenuOpen || m_model.sessions().empty()) {
    return;
  }
  std::vector<std::string> names;
  names.reserve(m_model.sessions().size());
  for (const auto& session : m_model.sessions()) {
    names.push_back(session.name);
  }
  buildMenu(
      names, m_model.state().selectedSession, m_sessionSelectBox, /*upward=*/false,
      /*rightAlign=*/false, /*zBase=*/50, m_sessionMenuPanel, m_sessionMenuRows, m_sessionMenuLabels,
      m_sessionMenuAreas, m_sessionMenuScroll, [this](std::size_t i) { selectSession(i); }
  );
}

void GreeterSurface::rebuildSchemeMenu() {
  clearSchemeMenu();
  if (!m_schemeMenuOpen || m_model.schemeNames().empty()) {
    return;
  }
  // Open upward when selector is at the bottom, downward otherwise
  const bool upward = m_schemeSelectBox->y() > m_root.height() / 2;
  buildMenu(
      m_model.schemeNames(), m_model.state().selectedScheme, m_schemeSelectBox,
      /*upward=*/upward,
      /*rightAlign=*/true, /*zBase=*/60, m_schemeMenuPanel, m_schemeMenuRows, m_schemeMenuLabels, m_schemeMenuAreas,
      m_schemeMenuScroll, [this](std::size_t i) { selectScheme(i); }
  );
}
