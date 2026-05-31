#include "greeter/greeter_surface.h"

#include "core/key_modifiers.h"
#include "core/key_symbols.h"
#include "core/log.h"
#include "core/resource_paths.h"
#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_window.h"
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
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <linux/input-event-codes.h>
#include <sstream>
#include <unordered_set>

namespace {
constexpr Logger kLog("greeter-surface");

Button::ButtonStateColors makePaletteState(ColorRole bg,
                                           std::optional<ColorRole> border,
                                           ColorRole label,
                                           float alpha = 1.0f) {
  return Button::ButtonStateColors{
      colorSpecFromRole(bg, alpha),
      border.has_value() ? colorSpecFromRole(*border, alpha) : clearColorSpec(),
      colorSpecFromRole(label, alpha),
  };
}

// User row button palette matches noctalia-shell ColorRole usage.
Button::ButtonPalette userRowPalette() {
  constexpr float kDisabledAlpha = 0.55f;
  return Button::ButtonPalette{
      .borderWidth = Style::borderWidth,
      .normal = makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline,
                                 ColorRole::OnSurface),
      .hover = makePaletteState(ColorRole::Secondary, std::nullopt,
                                ColorRole::OnSecondary),
      .pressed = makePaletteState(ColorRole::Primary, ColorRole::Primary,
                                  ColorRole::OnPrimary),
      .disabled =
          makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline,
                           ColorRole::OnSurface, kDisabledAlpha),
      .selected = makePaletteState(ColorRole::Primary, ColorRole::Primary,
                                   ColorRole::OnPrimary),
  };
}

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool parseColorWallpaperPath(std::string_view path, Color &out) {
  constexpr std::string_view kPrefix = "color:";
  if (!path.starts_with(kPrefix)) {
    return false;
  }
  return tryParseHexColor(path.substr(kPrefix.size()), out);
}

std::string sanitizeDesktopExec(const std::string &exec) {
  std::istringstream stream(exec);
  std::string token;
  std::string out;
  while (stream >> token) {
    if (!token.empty() && token[0] == '%') {
      continue;
    }
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += token;
  }
  return trim(out);
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

void GreeterSurface::initialize(GreeterWindow &window, RenderContext *context) {
  m_window = &window;
  m_renderContext = context;

  auto wallpaper = std::make_unique<WallpaperNode>();
  m_wallpaper = wallpaper.get();
  m_wallpaper->setZIndex(0);
  m_root.addChild(std::move(wallpaper));

  auto backdrop = std::make_unique<RectNode>();
  backdrop->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::Surface),
      .fillMode = FillMode::Solid,
  });
  m_backdrop = backdrop.get();
  m_backdrop->setZIndex(1);
  m_root.addChild(std::move(backdrop));

  auto title = std::make_unique<Label>();
  title->setText("Welcome");
  title->setFontSize(Style::fontSizeHeading + 2.0f);
  title->setBold(true);
  title->setColor(colorForRole(ColorRole::OnSurface));
  m_titleLabel = title.get();
  m_titleLabel->setZIndex(6);
  m_root.addChild(std::move(title));

  auto formSubtitle = std::make_unique<Label>();
  formSubtitle->setText("Please select your user");
  formSubtitle->setFontSize(12.0f);
  formSubtitle->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_formSubtitleLabel = formSubtitle.get();
  m_formSubtitleLabel->setZIndex(6);
  m_root.addChild(std::move(formSubtitle));

  auto brandPane = std::make_unique<RectNode>();
  brandPane->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::Surface, 0.95f),
      .fillMode = FillMode::Solid,
  });
  m_brandPane = brandPane.get();
  m_brandPane->setZIndex(5);
  m_root.addChild(std::move(brandPane));

  auto brand = std::make_unique<ImageNode>();
  brand->setFitMode(ImageFitMode::Contain);
  brand->setTint(colorForRole(ColorRole::OnSurface, 0.96f));
  m_brandLogo = brand.get();
  m_brandLogo->setZIndex(7);
  m_root.addChild(std::move(brand));

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
  divider->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::Outline, 0.90f),
      .fillMode = FillMode::Solid,
      .radius = 1.0f,
  });
  m_panelDivider = divider.get();
  m_root.addChild(std::move(divider));

  auto panel = std::make_unique<Box>();
  m_loginPanel = panel.get();
  m_loginPanel->setZIndex(1);
  m_root.addChild(std::move(panel));

  auto pwField = std::make_unique<Input>();
  pwField->setPlaceholder("Type password");
  pwField->setPasswordMode(true);
  pwField->setControlHeight(Style::controlHeight);
  pwField->setOnChange(
      [this](const std::string &value) { m_password = value; });
  pwField->setOnSubmit([this](const std::string &) { tryAuthenticate(); });
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
  userArea->setOnClick([this](const InputArea::PointerData &data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    toggleUserMenu();
  });
  m_userSelectArea = userArea.get();
  m_userSelectArea->setZIndex(7);
  m_root.addChild(std::move(userArea));

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
  sessionArea->setOnClick([this](const InputArea::PointerData &data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    toggleSessionMenu();
  });
  m_sessionSelectArea = sessionArea.get();
  m_sessionSelectArea->setZIndex(7);
  m_root.addChild(std::move(sessionArea));

  auto schemeBox = std::make_unique<Box>();
  m_schemeSelectBox = schemeBox.get();
  m_schemeSelectBox->setZIndex(6);
  m_root.addChild(std::move(schemeBox));

  auto schemeLabel = std::make_unique<Label>();
  schemeLabel->setFontSize(Style::fontSizeBody);
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
  schemeArea->setOnClick([this](const InputArea::PointerData &data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    toggleSchemeMenu();
  });
  m_schemeSelectArea = schemeArea.get();
  m_schemeSelectArea->setZIndex(7);
  m_root.addChild(std::move(schemeArea));

  auto loginBtn = std::make_unique<Button>();
  loginBtn->setGlyph("arrow-right");
  loginBtn->setGlyphSize(16.0f);
  loginBtn->setVariant(ButtonVariant::Primary);
  loginBtn->setContentAlign(ButtonContentAlign::Center);
  loginBtn->setOnClick([this]() { tryAuthenticate(); });
  m_loginButton = loginBtn.get();
  m_loginButton->setZIndex(6);
  m_root.addChild(std::move(loginBtn));

  auto backBtn = std::make_unique<Button>();
  backBtn->setGlyph("arrow-left");
  backBtn->setGlyphSize(16.0f);
  backBtn->setCustomPalette(Button::ButtonPalette{
      .borderWidth = Style::borderWidth,
      .normal = makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline,
                                 ColorRole::OnSurface),
      .hover = makePaletteState(ColorRole::Secondary, std::nullopt,
                                ColorRole::OnSecondary),
      .pressed = makePaletteState(ColorRole::Primary, ColorRole::Primary,
                                  ColorRole::OnPrimary),
      .disabled =
          makePaletteState(ColorRole::SurfaceVariant, ColorRole::Outline,
                           ColorRole::OnSurface, 0.55f),
      .selected = std::nullopt,
  });
  backBtn->setContentAlign(ButtonContentAlign::Center);
  backBtn->setOnClick([this]() { runBackAction(); });
  m_backButton = backBtn.get();
  m_backButton->setZIndex(6);
  m_root.addChild(std::move(backBtn));

  auto status = std::make_unique<Label>();
  status->setFontSize(Style::fontSizeCaption);
  status->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  m_statusLabel = status.get();
  m_statusLabel->setZIndex(6);
  m_root.addChild(std::move(status));

  m_root.setAnimationManager(&m_animations);
  m_inputDispatcher.setSceneRoot(&m_root);
  m_inputDispatcher.setCursorShapeCallback(
      [](std::uint32_t serial, std::uint32_t shape) {
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
  m_passwordVisible = false;

  const auto rowPalette = userRowPalette();
  m_userRowButtons.clear();
  m_userRowArrows.clear();
  m_userRowButtons.reserve(m_users.size());
  m_userRowArrows.reserve(m_users.size());
  for (std::size_t i = 0; i < m_users.size(); ++i) {
    auto rowBtn = std::make_unique<Button>();
    rowBtn->setCustomPalette(rowPalette);
    rowBtn->setText(m_users[i]);
    rowBtn->setFontSize(16.0f);
    rowBtn->setContentAlign(ButtonContentAlign::Start);
    rowBtn->setZIndex(6);
    rowBtn->setOnClick([this, i]() { enterPasswordStep(i); });
    auto *rowPtr = rowBtn.get();
    m_root.addChild(std::move(rowBtn));
    // Keep list-row state changes immediate to avoid hover flicker.
    rowPtr->setAnimationManager(nullptr);
    m_userRowButtons.push_back(rowPtr);

    auto arrow = std::make_unique<Glyph>();
    arrow->setGlyph("arrow-right");
    arrow->setGlyphSize(18.0f);
    arrow->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    arrow->setHitTestVisible(false);
    arrow->setZIndex(7);
    auto *arrowPtr = arrow.get();
    m_root.addChild(std::move(arrow));
    m_userRowArrows.push_back(arrowPtr);
  }

  const auto logoPath = paths::assetPath("noctalia.svg");
  // Large logo texture with mipmaps for smooth downscale.
  m_brandLogoTexture = m_renderContext->textureManager().loadFromFile(
      logoPath.string(), 1024, true);
  if (m_brandLogo != nullptr && m_brandLogoTexture.id != 0) {
    m_brandLogo->setTextureId(m_brandLogoTexture.id);
    m_brandLogo->setTextureSize(m_brandLogoTexture.width,
                                m_brandLogoTexture.height);
  } else {
    kLog.warn("failed loading logo texture from {}", logoPath.string());
  }

  requestLayout();
}

void GreeterSurface::setGreetdClient(GreetdClient *client) {
  m_greetdClient = client;
}

void GreeterSurface::setUsername(const std::string &username) {
  m_username = username;
}

void GreeterSurface::setOnExitRequested(std::function<void()> callback) {
  m_onExitRequested = std::move(callback);
}

void GreeterSurface::onPointerEvent(float x, float y, std::uint32_t button,
                                    bool pressed) {
  if (pressed && button == BTN_LEFT &&
      (m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen)) {
    const auto inRect = [x, y](const Node *node) {
      if (node == nullptr) {
        return false;
      }
      return x >= node->x() && y >= node->y() &&
             x <= node->x() + node->width() && y <= node->y() + node->height();
    };

    const bool onUserAnchor = inRect(m_userSelectBox);
    const bool onSessionAnchor = inRect(m_sessionSelectBox);
    const bool onSchemeAnchor = inRect(m_schemeSelectBox);
    const bool onUserMenu = inRect(m_userMenuPanel);
    const bool onSessionMenu = inRect(m_sessionMenuPanel);
    const bool onSchemeMenu = inRect(m_schemeMenuPanel);
    if (!onUserAnchor && !onSessionAnchor && !onSchemeAnchor && !onUserMenu &&
        !onSessionMenu && !onSchemeMenu) {
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

void GreeterSurface::onKeyEvent(std::uint32_t sym, std::uint32_t utf32,
                                std::uint32_t modifiers, bool pressed,
                                bool preedit) {
  if (!pressed)
    return;
  m_inInputDispatch = true;
  if (!handleNavigationKey(sym, modifiers)) {
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
  if (m_window) {
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
  if (m_window) {
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

void GreeterSurface::prepareFrame(bool /*needsUpdate*/, bool needsLayout) {
  if (!m_renderContext || !m_window) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!m_animTickInitialized) {
    m_lastAnimTick = now;
    m_animTickInitialized = true;
  } else {
    const float deltaMs =
        std::chrono::duration<float, std::milli>(now - m_lastAnimTick).count();
    m_lastAnimTick = now;
    if (deltaMs > 0.0f) {
      m_animations.tick(deltaMs);
    }
  }

  if (needsLayout) {
    m_renderContext->syncContentScale(m_window->renderTarget());
    syncWallpaperTexture();
    m_inLayout = true;
    layoutScene(m_window->width(), m_window->height());
    m_inLayout = false;
  }
}

void GreeterSurface::enterPasswordStep(std::size_t userIndex) {
  if (userIndex >= m_users.size()) {
    return;
  }
  m_selectedUser = userIndex;
  setUsername(m_users[userIndex]);
  m_passwordVisible = true;
  m_passwordField->setValue("");
  m_password.clear();
  if (m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
  }
  refreshSelectionLabels();
  requestLayout();
}

void GreeterSurface::layoutScene(std::uint32_t width, std::uint32_t height) {
  auto *renderer = m_renderContext;
  if (!renderer)
    return;

  const float sw = static_cast<float>(width);
  const float sh = static_cast<float>(height);

  m_root.setSize(sw, sh);

  if (m_wallpaper != nullptr) {
    m_wallpaper->setPosition(0.0f, 0.0f);
    m_wallpaper->setSize(sw, sh);
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  }

  m_backdrop->setPosition(0.0f, 0.0f);
  m_backdrop->setSize(sw, sh);
  static_cast<RectNode *>(m_backdrop)
      ->setStyle(RoundedRectStyle{
          .fill = m_hasSyncedWallpaper ? colorForRole(ColorRole::Surface, 0.0f)
                                       : colorForRole(ColorRole::Surface),
          .fillMode = FillMode::Solid,
      });
  m_backdrop->setVisible(!m_hasSyncedWallpaper ||
                         m_wallpaperFillColor.a > 0.0f);

  const float panelWidth = std::clamp(sw * 0.30f, 420.0f, 520.0f);
  const float rowHeight = Style::controlHeight + Style::spaceSm;
  const float rowGap = Style::spaceSm;
  const float panelPadding = Style::spaceLg;

  const bool hasLogo = m_brandLogoTexture.id != 0;
  constexpr float kBaseLogoSize = 56.0f;
  const float userCount = static_cast<float>(m_userRowButtons.size());
  const float logoScale =
      std::clamp(1.0f - (userCount - 2.0f) * 0.08f, 0.75f, 1.0f);
  const float logoSize = hasLogo ? (kBaseLogoSize * logoScale) : 0.0f;
  const float logoBlockHeight = hasLogo ? (logoSize + Style::spaceMd) : 0.0f;

  if (m_passwordVisible) {
    m_titleLabel->setText("Enter password");
    if (!m_users.empty() && m_selectedUser < m_users.size()) {
      m_formSubtitleLabel->setText(m_users[m_selectedUser]);
    } else {
      m_formSubtitleLabel->setText("Please authenticate");
    }
  } else {
    m_titleLabel->setText("Welcome");
    m_formSubtitleLabel->setText("Please select your user");
  }
  m_titleLabel->measure(*renderer);
  m_formSubtitleLabel->measure(*renderer);
  m_statusLabel->measure(*renderer);

  const float headerTextHeight =
      m_titleLabel->height() + 6.0f + m_formSubtitleLabel->height();
  const float headerToContentGap = Style::spaceLg;
  const float headerBlockHeight =
      logoBlockHeight + headerTextHeight + headerToContentGap;

  float contentBlockHeight = 0.0f;
  if (m_passwordVisible) {
    contentBlockHeight = Style::controlHeight;
  } else if (!m_userRowButtons.empty()) {
    contentBlockHeight =
        rowHeight * static_cast<float>(m_userRowButtons.size()) +
        rowGap * static_cast<float>(m_userRowButtons.size() - 1);
  }

  const bool hasStatus = !m_status.empty();
  const float statusGap = hasStatus ? Style::spaceSm : 0.0f;
  const float statusHeight = hasStatus ? m_statusLabel->height() : 0.0f;
  const float panelTopPadding = panelPadding;
  const float panelBottomPadding =
      hasStatus ? panelPadding
                : (m_passwordVisible ? Style::spaceSm : panelPadding);
  const float panelInnerHeight =
      headerBlockHeight + contentBlockHeight + statusGap + statusHeight;
  const float minPanelHeight = 0.0f;
  const float maxPanelHeight =
      std::max(minPanelHeight, sh - panelPadding * 2.0f);
  const float panelHeight =
      std::clamp(panelInnerHeight + panelTopPadding + panelBottomPadding,
                 minPanelHeight, maxPanelHeight);
  const float panelX = std::round((sw - panelWidth) * 0.5f);
  const float panelY = std::round((sh - panelHeight) * 0.5f);
  const float contentLeft = panelX + panelPadding;
  const float contentWidth = panelWidth - panelPadding * 2.0f;

  float headerY = panelY + panelTopPadding;

  if (m_brandPane != nullptr) {
    static_cast<RectNode *>(m_brandPane)->setVisible(false);
  }

  // Refresh text colors each layout for theme/scheme changes.
  m_titleLabel->setColor(colorForRole(ColorRole::OnSurface));
  m_formSubtitleLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  if (m_brandTitleLabel != nullptr) {
    m_brandTitleLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_brandSubtitleLabel != nullptr) {
    m_brandSubtitleLabel->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_statusLabel != nullptr) {
    m_statusLabel->setColor(m_statusIsError
                                ? colorForRole(ColorRole::Error)
                                : colorForRole(ColorRole::OnSurfaceVariant));
  }

  if (m_brandLogo != nullptr) {
    m_brandLogo->setVisible(hasLogo);
    if (hasLogo) {
      m_brandLogo->setSize(logoSize, logoSize);
      m_brandLogo->setPosition(
          std::round(panelX + (panelWidth - logoSize) * 0.5f), headerY);
      m_brandLogo->setTint(colorForRole(ColorRole::OnSurface, 1.0f));
      headerY += logoSize + Style::spaceMd;
    }
  }

  m_titleLabel->setPosition(
      std::round(panelX + (panelWidth - m_titleLabel->width()) * 0.5f),
      headerY);
  m_formSubtitleLabel->setPosition(
      std::round(panelX + (panelWidth - m_formSubtitleLabel->width()) * 0.5f),
      headerY + m_titleLabel->height() + 6.0f);
  const float contentTop = headerY + headerTextHeight + headerToContentGap;

  if (m_panelDivider != nullptr)
    m_panelDivider->setVisible(false);

  m_loginPanel->setPosition(panelX, panelY);
  m_loginPanel->setSize(panelWidth, panelHeight);
  m_loginPanel->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::SurfaceVariant, 1.0f),
      .border = colorForRole(ColorRole::Outline, 1.0f),
      .fillMode = FillMode::Solid,
      .radius = Style::scaledRadiusXl(),
      .softness = 1.0f,
      .borderWidth = Style::borderWidth,
  });

  const float buttonWidth = Style::controlHeight;
  const float gap = Style::spaceSm;
  const float inputWidth = std::max(120.0f, contentWidth - buttonWidth - gap);

  const auto selectorStyle = RoundedRectStyle{
      .fill = colorForRole(ColorRole::SurfaceVariant),
      .border = colorForRole(ColorRole::Outline),
      .fillMode = FillMode::Solid,
      .radius = Style::scaledRadiusMd(),
      .softness = 1.0f,
      .borderWidth = Style::borderWidth,
  };

  // Color scheme selector (top-right).
  const float schemeW = 210.0f;
  const float schemeH = 38.0f;
  const float schemeX = sw - schemeW - Style::spaceLg;
  const float schemeY = Style::spaceLg;
  m_schemeSelectBox->setVisible(true);
  m_schemeSelectLabel->setVisible(true);
  m_schemeSelectGlyph->setVisible(true);
  m_schemeSelectArea->setVisible(true);
  m_schemeSelectBox->setStyle(selectorStyle);
  m_schemeSelectBox->setPosition(schemeX, schemeY);
  m_schemeSelectBox->setSize(schemeW, schemeH);
  m_schemeSelectBox->layout(*renderer);
  m_schemeSelectLabel->measure(*renderer);
  m_schemeSelectLabel->setPosition(
      schemeX + Style::spaceSm,
      schemeY + std::round((schemeH - m_schemeSelectLabel->height()) * 0.5f));
  (void)m_schemeSelectGlyph->measure(*renderer);
  {
    const auto glyphMetrics = renderer->measureGlyph(
        m_schemeSelectGlyph->codepoint(), m_schemeSelectGlyph->fontSize());
    const float glyphW = glyphMetrics.right - glyphMetrics.left;
    const float glyphY =
        schemeY + std::round(schemeH * 0.5f -
                             (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
    m_schemeSelectGlyph->setPosition(
        schemeX + schemeW - Style::spaceSm - glyphW, glyphY);
  }
  m_schemeSelectArea->setPosition(schemeX, schemeY);
  m_schemeSelectArea->setSize(schemeW, schemeH);

  m_userSelectBox->setVisible(false);
  m_userSelectLabel->setVisible(false);
  m_userSelectGlyph->setVisible(false);
  m_userSelectArea->setVisible(false);

  for (std::size_t i = 0; i < m_userRowButtons.size(); ++i) {
    const float rowY =
        contentTop + static_cast<float>(i) * (rowHeight + rowGap);
    Button *rowBtn = m_userRowButtons[i];
    rowBtn->setVisible(!m_passwordVisible);
    rowBtn->setText(m_users[i]);
    rowBtn->setSelected(i == m_selectedUser);
    rowBtn->setPosition(contentLeft, rowY);
    rowBtn->setSize(contentWidth, rowHeight);
    rowBtn->layout(*renderer);

    if (Label *label = rowBtn->label()) {
      label->measure(*renderer);
      const float labelX = Style::spaceMd;
      const float labelY = std::round((rowHeight - label->height()) * 0.5f);
      label->setPosition(labelX, labelY);
    }

    if (i < m_userRowArrows.size() && m_userRowArrows[i] != nullptr) {
      auto *arrow = m_userRowArrows[i];
      arrow->setVisible(!m_passwordVisible);
      arrow->setColor(i == m_selectedUser
                          ? colorForRole(ColorRole::OnPrimary)
                          : colorForRole(ColorRole::OnSurfaceVariant));
      (void)arrow->measure(*renderer);
      const auto glyphMetrics =
          renderer->measureGlyph(arrow->codepoint(), arrow->fontSize());
      const float glyphW = glyphMetrics.right - glyphMetrics.left;
      const float glyphY =
          rowY + std::round(rowHeight * 0.5f -
                            (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
      arrow->setPosition(contentLeft + contentWidth - Style::spaceMd - glyphW,
                         glyphY);
    }
  }

  m_passwordField->setVisible(m_passwordVisible);
  m_loginButton->setVisible(m_passwordVisible);
  m_backButton->setVisible(m_passwordVisible);
  if (m_passwordVisible) {
    const float backSize = Style::controlHeight;
    m_backButton->setSize(backSize, backSize);
    m_backButton->setPosition(contentLeft, panelY + panelPadding);
    m_backButton->layout(*renderer);
    if (Glyph *backGlyph = m_backButton->glyph()) {
      (void)backGlyph->measure(*renderer);
      const auto glyphMetrics =
          renderer->measureGlyph(backGlyph->codepoint(), backGlyph->fontSize());
      const float glyphW = glyphMetrics.right - glyphMetrics.left;
      const float glyphH = glyphMetrics.bottom - glyphMetrics.top;
      const float glyphY = std::round(
          backSize * 0.5f - (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
      backGlyph->setPosition(
          std::round(backSize * 0.5f -
                     (glyphMetrics.left + glyphMetrics.right) * 0.5f),
          glyphY);
      backGlyph->setSize(std::max(glyphW, 1.0f), std::max(glyphH, 1.0f));
    }

    m_passwordField->setSize(inputWidth, 0.0f);
    m_passwordField->setPosition(contentLeft, contentTop);
    m_passwordField->layout(*renderer);

    m_loginButton->setSize(buttonWidth, Style::controlHeight);
    m_loginButton->setPosition(contentLeft + inputWidth + gap, contentTop);
    m_loginButton->layout(*renderer);
    if (Glyph *loginGlyph = m_loginButton->glyph()) {
      (void)loginGlyph->measure(*renderer);
      const auto glyphMetrics = renderer->measureGlyph(loginGlyph->codepoint(),
                                                       loginGlyph->fontSize());
      const float glyphW = glyphMetrics.right - glyphMetrics.left;
      const float glyphH = glyphMetrics.bottom - glyphMetrics.top;
      const float glyphY =
          std::round(Style::controlHeight * 0.5f -
                     (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
      loginGlyph->setPosition(
          std::round(buttonWidth * 0.5f -
                     (glyphMetrics.left + glyphMetrics.right) * 0.5f),
          glyphY);
      loginGlyph->setSize(std::max(glyphW, 1.0f), std::max(glyphH, 1.0f));
    }
  } else {
    m_backButton->setVisible(false);
  }

  if (hasStatus) {
    const float statusY = contentTop + contentBlockHeight + statusGap;
    m_statusLabel->setVisible(true);
    m_statusLabel->setPosition(contentLeft, statusY);
  } else {
    m_statusLabel->setVisible(false);
  }

  // Session selector fixed in bottom-left corner.
  const float sessionW = 180.0f;
  const float sessionH = 38.0f;
  const float sessionX = Style::spaceLg;
  const float sessionY = sh - sessionH - Style::spaceLg;
  m_sessionSelectBox->setVisible(true);
  m_sessionSelectLabel->setVisible(true);
  m_sessionSelectGlyph->setVisible(true);
  m_sessionSelectArea->setVisible(true);
  m_sessionSelectBox->setStyle(selectorStyle);
  m_sessionSelectBox->setPosition(sessionX, sessionY);
  m_sessionSelectBox->setSize(sessionW, sessionH);
  m_sessionSelectBox->layout(*renderer);
  m_sessionSelectLabel->measure(*renderer);
  m_sessionSelectLabel->setPosition(
      sessionX + Style::spaceSm,
      sessionY +
          std::round((sessionH - m_sessionSelectLabel->height()) * 0.5f));
  (void)m_sessionSelectGlyph->measure(*renderer);
  {
    const auto glyphMetrics = renderer->measureGlyph(
        m_sessionSelectGlyph->codepoint(), m_sessionSelectGlyph->fontSize());
    const float glyphW = glyphMetrics.right - glyphMetrics.left;
    const float glyphY =
        sessionY + std::round(sessionH * 0.5f -
                              (glyphMetrics.top + glyphMetrics.bottom) * 0.5f);
    m_sessionSelectGlyph->setPosition(
        sessionX + sessionW - Style::spaceSm - glyphW, glyphY);
  }
  m_sessionSelectArea->setPosition(sessionX, sessionY);
  m_sessionSelectArea->setSize(sessionW, sessionH);

  rebuildUserMenu();
  rebuildSessionMenu();
  rebuildSchemeMenu();

  rebuildFocusRing();
  applyMenuHighlight();

  // Place initial keyboard focus on the last-used user so a returning user can
  // type their password immediately. Done here because the user rows only have
  // valid focus targets once the scene has been laid out.
  if (!m_initialFocusDone && !m_passwordVisible && !m_focusRing.empty()) {
    std::ptrdiff_t initial = 0;
    for (std::size_t i = 0; i < m_focusRing.size(); ++i) {
      if (m_selectedUser < m_userRowButtons.size() &&
          m_userRowButtons[m_selectedUser] != nullptr &&
          m_focusRing[i].area ==
              m_userRowButtons[m_selectedUser]->inputArea()) {
        initial = static_cast<std::ptrdiff_t>(i);
        break;
      }
    }
    setFocusIndex(initial);
    m_initialFocusDone = true;
  }
}

void GreeterSurface::tryAuthenticate() {
  if (!m_greetdClient || m_authenticating)
    return;
  if (m_username.empty()) {
    updateStatus("Enter a username", true);
    return;
  }
  if (m_password.empty()) {
    updateStatus("Enter a password", true);
    return;
  }

  m_authenticating = true;
  updateStatus("Authenticating...", false);

  if (!m_authSessionStarted) {
    auto initialMsg = m_greetdClient->createSession(m_username);
    if (m_greetdClient->lastError()) {
      onAuthError(m_greetdClient->lastError()->description);
      return;
    }
    m_authSessionStarted = true;
    if (initialMsg.has_value() && !initialMsg->message.empty()) {
      updateStatus(initialMsg->message, false);
    }
  }

  auto msg = m_greetdClient->postAuthData(m_password);

  if (m_greetdClient->lastError()) {
    onAuthError(m_greetdClient->lastError()->description);
    return;
  }

  if (msg.has_value()) {
    onAuthMessage(msg);
  } else {
    onAuthSuccess();
  }
}

void GreeterSurface::onAuthMessage(
    const std::optional<GreetdAuthMessage> &msg) {
  m_authenticating = false;
  if (msg.has_value()) {
    updateStatus(msg->message, false);
  }
}

void GreeterSurface::onAuthSuccess() {
  m_authenticating = false;
  updateStatus("Login successful, starting session...", false);
  kLog.info("authentication successful");

  if (!m_greetdClient) {
    kLog.error("no greetd client to start session");
    return;
  }

  GreetdSessionCommand cmd;
  if (!m_sessions.empty() && m_selectedSession < m_sessions.size()) {
    cmd.command = m_sessions[m_selectedSession].command;
  } else {
    cmd.command = "/bin/sh";
  }

  if (!m_greetdClient->startSession(cmd)) {
    if (m_greetdClient->lastError()) {
      kLog.error("start_session failed: {}",
                 m_greetdClient->lastError()->description);
      updateStatus("Failed to start session: " +
                       m_greetdClient->lastError()->description,
                   true);
    }
    return;
  }

  kLog.info("session start requested, exiting greeter");
  if (m_onExitRequested) {
    m_onExitRequested();
  }
}

void GreeterSurface::onAuthError(const std::string &error) {
  m_authenticating = false;
  m_authSessionStarted = false;
  updateStatus(error, true);
  m_passwordField->setValue("");
  kLog.warn("authentication failed: {}", error);
}

void GreeterSurface::updateStatus(const std::string &text, bool isError) {
  m_status = text;
  m_statusIsError = isError;
  if (m_statusLabel) {
    m_statusLabel->setText(text);
    m_statusLabel->setColor(isError
                                ? colorForRole(ColorRole::Error)
                                : colorForRole(ColorRole::OnSurfaceVariant));
  }
  requestRedraw();
}

void GreeterSurface::loadUsers() {
  m_users.clear();
  static const std::unordered_set<std::string> kHiddenSystemUsers = {
      "greeter", "greetd", "sddm", "lightdm", "gdm", "nobody",
  };
  std::ifstream passwd("/etc/passwd");
  std::string line;
  while (std::getline(passwd, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::stringstream fields(line);
    std::string user;
    std::string ignored;
    std::string uidText;
    std::string shell;
    std::getline(fields, user, ':');
    std::getline(fields, ignored, ':');
    std::getline(fields, uidText, ':');
    std::getline(fields, ignored, ':');
    std::getline(fields, ignored, ':');
    std::getline(fields, ignored, ':');
    std::getline(fields, shell, ':');
    if (user.empty() || uidText.empty() || shell.empty()) {
      continue;
    }
    int uid = 0;
    try {
      uid = std::stoi(uidText);
    } catch (...) {
      continue;
    }
    if (uid < 1000 || kHiddenSystemUsers.contains(user)) {
      continue;
    }
    if (shell.find("nologin") != std::string::npos ||
        shell.find("false") != std::string::npos) {
      continue;
    }
    m_users.push_back(user);
  }

  if (m_users.empty()) {
    m_users.push_back("greeter");
  }
  m_selectedUser = 0;
  setUsername(m_users[m_selectedUser]);
}

void GreeterSurface::loadSessions() {
  m_sessions.clear();
  const std::array<std::filesystem::path, 2> dirs = {
      "/usr/share/wayland-sessions",
      "/usr/local/share/wayland-sessions",
  };

  for (const auto &dir : dirs) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
      continue;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      if (entry.path().extension() != ".desktop") {
        continue;
      }

      std::ifstream in(entry.path());
      std::string line;
      std::string name;
      std::string exec;
      while (std::getline(in, line)) {
        if (line.rfind("Name=", 0) == 0) {
          name = trim(line.substr(5));
        } else if (line.rfind("Exec=", 0) == 0) {
          exec = sanitizeDesktopExec(line.substr(5));
        }
      }

      if (!name.empty() && !exec.empty()) {
        m_sessions.push_back(SessionOption{.name = name, .command = exec});
      }
    }
  }

  if (m_sessions.empty()) {
    m_sessions.push_back(SessionOption{.name = "Shell", .command = "/bin/sh"});
  }
  m_selectedSession = 0;
}

void GreeterSurface::refreshSelectionLabels() {
  if (m_userSelectLabel != nullptr) {
    const std::string userLabel =
        m_users.empty() ? "(none)"
                        : m_users[std::min(m_selectedUser, m_users.size() - 1)];
    m_userSelectLabel->setText(userLabel);
    m_userSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_sessionSelectLabel != nullptr) {
    const std::string sessionLabel =
        m_sessions.empty()
            ? "/bin/sh"
            : m_sessions[std::min(m_selectedSession, m_sessions.size() - 1)]
                  .name;
    m_sessionSelectLabel->setText(sessionLabel);
    m_sessionSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_userSelectGlyph != nullptr) {
    m_userSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_sessionSelectGlyph != nullptr) {
    m_sessionSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
  if (m_schemeSelectLabel != nullptr) {
    const std::string schemeLabel =
        m_schemeNames.empty()
            ? "Noctalia"
            : m_schemeNames[std::min(m_selectedScheme,
                                     m_schemeNames.size() - 1)];
    m_schemeSelectLabel->setText(schemeLabel);
    m_schemeSelectLabel->setColor(colorForRole(ColorRole::OnSurface));
  }
  if (m_schemeSelectGlyph != nullptr) {
    m_schemeSelectGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  }
}

void GreeterSurface::buildSchemeNames() {
  m_schemeNames.clear();
  m_syncedAppearance = loadGreeterSyncedAppearance();
  if (m_syncedAppearance.has_value()) {
    m_schemeNames.emplace_back(greeter::appearance::kSyncedSchemeDisplayName);
    m_selectedScheme = 0;
  }

  for (const auto &builtinPalette : noctalia::theme::builtinPalettes()) {
    m_schemeNames.emplace_back(builtinPalette.name);
    if (!m_syncedAppearance.has_value() && builtinPalette.name == "Noctalia") {
      m_selectedScheme = m_schemeNames.size() - 1;
    }
  }
}

bool GreeterSurface::isSyncedScheme(const std::size_t schemeIndex) const {
  return schemeIndex < m_schemeNames.size() &&
         m_schemeNames[schemeIndex] ==
             greeter::appearance::kSyncedSchemeDisplayName;
}

std::optional<std::size_t>
GreeterSurface::findSchemeIndex(const std::string_view name) const {
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
    m_wallpaperPath = m_syncedAppearance->wallpaperPath;
    m_wallpaperFillMode = m_syncedAppearance->wallpaperFillMode;
    m_wallpaperFillColor = m_syncedAppearance->wallpaperFillColor;
    m_hasSyncedWallpaper = !m_wallpaperPath.empty();
    m_wallpaperDirty = true;
    return;
  }

  if (const auto *builtinPalette =
          noctalia::theme::findBuiltinPalette(m_schemeNames[schemeIndex])) {
    setPalette(builtinPalette->dark.palette);
  }
  clearWallpaperDisplay();
}

void GreeterSurface::syncWallpaperTexture() {
  if (!m_wallpaperDirty || m_wallpaper == nullptr ||
      m_renderContext == nullptr) {
    return;
  }

  if (m_wallpaperTexture.id != 0) {
    m_renderContext->textureManager().unload(m_wallpaperTexture);
    m_wallpaperTexture = {};
  }

  Color color;
  if (parseColorWallpaperPath(m_wallpaperPath, color)) {
    m_wallpaper->setSources(WallpaperSourceKind::Color, {}, color,
                            WallpaperSourceKind::Color, {}, color, 0.0f, 0.0f,
                            0.0f, 0.0f);
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f,
                               TransitionParams{});
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  } else if (!m_wallpaperPath.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(m_wallpaperPath, ec) && !ec) {
      m_wallpaperTexture = m_renderContext->textureManager().loadFromFile(
          m_wallpaperPath, 0, true);
      if (m_wallpaperTexture.id != 0) {
        m_wallpaper->setTextures(m_wallpaperTexture.id, {},
                                 static_cast<float>(m_wallpaperTexture.width),
                                 static_cast<float>(m_wallpaperTexture.height),
                                 0.0f, 0.0f);
        m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f,
                                   TransitionParams{});
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

  if (prefs.session.has_value()) {
    for (std::size_t i = 0; i < m_sessions.size(); ++i) {
      if (m_sessions[i].name == *prefs.session) {
        m_selectedSession = i;
        break;
      }
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
    kLog.warn("failed to save greeter.conf (check permissions on {})",
              greeter::greeterConfPath().string());
  }
}

void GreeterSurface::toggleUserMenu() {
  m_userMenuOpen = !m_userMenuOpen;
  if (m_userMenuOpen) {
    m_sessionMenuOpen = false;
    clearSessionMenu();
  }
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
  m_menuHighlight = -1;
  clearUserMenu();
  clearSessionMenu();
  clearSchemeMenu();
}

void GreeterSurface::closeMenusAndRestoreFocus() {
  InputArea *owner = m_sessionMenuOpen  ? m_sessionSelectArea
                     : m_schemeMenuOpen ? m_schemeSelectArea
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
  requestLayout();
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
  requestLayout();
  requestRedraw();
}

void GreeterSurface::runBackAction() {
  m_passwordVisible = false;
  m_passwordField->setValue("");
  m_password.clear();
  updateStatus("", false);
  if (m_selectedUser < m_userRowButtons.size() &&
      m_userRowButtons[m_selectedUser] != nullptr &&
      m_userRowButtons[m_selectedUser]->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_userRowButtons[m_selectedUser]->inputArea());
  }
  requestLayout();
}

bool GreeterSurface::menuOpen() const noexcept {
  return m_userMenuOpen || m_sessionMenuOpen || m_schemeMenuOpen;
}

void GreeterSurface::rebuildFocusRing() {
  InputArea *previouslyFocused = InputArea::getFocused();
  m_focusRing.clear();

  if (m_passwordVisible) {
    if (m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
      m_focusRing.push_back({m_passwordField->inputArea(), {}});
    }
    if (m_loginButton != nullptr && m_loginButton->inputArea() != nullptr) {
      m_focusRing.push_back(
          {m_loginButton->inputArea(), [this]() { tryAuthenticate(); }});
    }
    if (m_backButton != nullptr && m_backButton->inputArea() != nullptr) {
      m_focusRing.push_back(
          {m_backButton->inputArea(), [this]() { runBackAction(); }});
    }
  } else {
    for (std::size_t i = 0; i < m_userRowButtons.size(); ++i) {
      Button *row = m_userRowButtons[i];
      if (row == nullptr || row->inputArea() == nullptr) {
        continue;
      }
      m_focusRing.push_back({row->inputArea(), [this, i]() {
                               enterPasswordStep(i);
                             }});
    }
  }

  if (m_sessionSelectArea != nullptr) {
    m_focusRing.push_back(
        {m_sessionSelectArea, [this]() { toggleSessionMenu(); }});
  }
  if (m_schemeSelectArea != nullptr) {
    m_focusRing.push_back(
        {m_schemeSelectArea, [this]() { toggleSchemeMenu(); }});
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
  const std::ptrdiff_t start = m_focusIndex < 0 ? 0 : m_focusIndex + delta;
  setFocusIndex(start);
}

void GreeterSurface::activateFocused() {
  if (m_focusIndex < 0 ||
      m_focusIndex >= static_cast<std::ptrdiff_t>(m_focusRing.size())) {
    return;
  }
  const auto &activate =
      m_focusRing[static_cast<std::size_t>(m_focusIndex)].activate;
  if (activate) {
    activate();
  }
}

void GreeterSurface::applyMenuHighlight() {
  std::vector<Box *> *rows = nullptr;
  std::size_t selected = 0;
  if (m_sessionMenuOpen) {
    rows = &m_sessionMenuRows;
    selected = m_selectedSession;
  } else if (m_schemeMenuOpen) {
    rows = &m_schemeMenuRows;
    selected = m_selectedScheme;
  } else if (m_userMenuOpen) {
    rows = &m_userMenuRows;
    selected = m_selectedUser;
  }

  if (rows == nullptr || rows->empty()) {
    m_menuHighlight = -1;
    return;
  }

  const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(rows->size());
  if (m_menuHighlight < 0 || m_menuHighlight >= count) {
    m_menuHighlight =
        std::min(static_cast<std::ptrdiff_t>(selected), count - 1);
  }

  for (std::ptrdiff_t i = 0; i < count; ++i) {
    Box *row = (*rows)[static_cast<std::size_t>(i)];
    if (row == nullptr) {
      continue;
    }
    row->setStyle(RoundedRectStyle{
        .fill = (i == m_menuHighlight)
                    ? colorForRole(ColorRole::Hover, 0.35f)
                    : colorForRole(ColorRole::SurfaceVariant, 0.01f),
        .fillMode = FillMode::Solid,
    });
  }
}

void GreeterSurface::moveMenuHighlight(int delta) {
  std::vector<Box *> *rows = m_sessionMenuOpen  ? &m_sessionMenuRows
                             : m_schemeMenuOpen ? &m_schemeMenuRows
                             : m_userMenuOpen   ? &m_userMenuRows
                                                : nullptr;
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

void GreeterSurface::activateMenuHighlight() {
  if (m_menuHighlight < 0) {
    return;
  }
  const std::size_t idx = static_cast<std::size_t>(m_menuHighlight);
  if (m_sessionMenuOpen) {
    selectSession(idx);
  } else if (m_schemeMenuOpen) {
    selectScheme(idx);
  }
}

bool GreeterSurface::handleNavigationKey(std::uint32_t sym,
                                         std::uint32_t modifiers) {
  if (m_authenticating) {
    return false;
  }
  const bool shift = (modifiers & KeyMod::Shift) != 0;
  const bool backward = shift || sym == XKB_KEY_ISO_Left_Tab;

  // A dropdown menu being open takes total precedence over the focus ring.
  if (menuOpen()) {
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
  InputArea *focused = InputArea::getFocused();
  if (m_passwordField != nullptr &&
      focused == m_passwordField->inputArea()) {
    return false;
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
    activateFocused();
    return true;
  }

  return false;
}

void GreeterSurface::clearUserMenu() {
  m_inputDispatcher.invalidateTransientPointers();
  if (m_userMenuPanel != nullptr) {
    (void)m_root.removeChild(m_userMenuPanel);
    m_userMenuPanel = nullptr;
  }
  for (auto *label : m_userMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto *area : m_userMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto *row : m_userMenuRows) {
    (void)m_root.removeChild(row);
  }
  m_userMenuLabels.clear();
  m_userMenuAreas.clear();
  m_userMenuRows.clear();
}

void GreeterSurface::clearSessionMenu() {
  m_inputDispatcher.invalidateTransientPointers();
  if (m_sessionMenuPanel != nullptr) {
    (void)m_root.removeChild(m_sessionMenuPanel);
    m_sessionMenuPanel = nullptr;
  }
  for (auto *label : m_sessionMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto *area : m_sessionMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto *row : m_sessionMenuRows) {
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
  for (auto *label : m_schemeMenuLabels) {
    (void)m_root.removeChild(label);
  }
  for (auto *area : m_schemeMenuAreas) {
    (void)m_root.removeChild(area);
  }
  for (auto *row : m_schemeMenuRows) {
    (void)m_root.removeChild(row);
  }
  m_schemeMenuLabels.clear();
  m_schemeMenuAreas.clear();
  m_schemeMenuRows.clear();
}

void GreeterSurface::rebuildUserMenu() {
  clearUserMenu();
  if (!m_userMenuOpen || m_users.empty()) {
    return;
  }

  const float x = m_userSelectBox->x();
  const float y =
      m_userSelectBox->y() + m_userSelectBox->height() + Style::spaceXs;
  const float w = m_userSelectBox->width();
  const float rowH = Style::controlHeightSm;
  const std::size_t count = m_users.size();
  const float h = rowH * static_cast<float>(count);

  auto panel = std::make_unique<Box>();
  m_userMenuPanel = panel.get();
  m_userMenuPanel->setZIndex(50);
  m_root.addChild(std::move(panel));
  m_userMenuPanel->setPosition(x, y);
  m_userMenuPanel->setSize(w, h);
  m_userMenuPanel->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::SurfaceVariant),
      .border = colorForRole(ColorRole::Outline),
      .fillMode = FillMode::Solid,
      .radius = Style::scaledRadiusMd(),
      .softness = 1.0f,
      .borderWidth = Style::borderWidth,
  });

  for (std::size_t i = 0; i < count; ++i) {
    auto row = std::make_unique<Box>();
    auto *rowPtr = row.get();
    rowPtr->setZIndex(51);
    rowPtr->setPosition(x, y + rowH * static_cast<float>(i));
    rowPtr->setSize(w, rowH);
    rowPtr->setStyle(RoundedRectStyle{
        .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
        .fillMode = FillMode::Solid,
    });
    m_root.addChild(std::move(row));
    m_userMenuRows.push_back(rowPtr);

    auto label = std::make_unique<Label>();
    auto *labelPtr = label.get();
    labelPtr->setText(m_users[i]);
    labelPtr->setFontSize(Style::fontSizeBody);
    labelPtr->setColor(i == m_selectedUser
                           ? colorForRole(ColorRole::Primary)
                           : colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(52);
    m_root.addChild(std::move(label));
    labelPtr->measure(*m_renderContext);
    labelPtr->setPosition(x + Style::spaceMd,
                          y + rowH * static_cast<float>(i) +
                              std::round((rowH - labelPtr->height()) * 0.5f));
    m_userMenuLabels.push_back(labelPtr);

    auto area = std::make_unique<InputArea>();
    auto *areaPtr = area.get();
    areaPtr->setFocusable(true);
    areaPtr->setZIndex(53);
    areaPtr->setOnEnter([this, i](const InputArea::PointerData &) {
      if (i < m_userMenuRows.size() && m_userMenuRows[i] != nullptr) {
        m_userMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::Hover, 0.35f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnLeave([this, i]() {
      if (i < m_userMenuRows.size() && m_userMenuRows[i] != nullptr) {
        m_userMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnClick([this, i](const InputArea::PointerData &data) {
      if (data.button != BTN_LEFT) {
        return;
      }
      m_selectedUser = i;
      setUsername(m_users[m_selectedUser]);
      refreshSelectionLabels();
      m_userMenuOpen = false;
      requestLayout();
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(x, y + rowH * static_cast<float>(i));
    areaPtr->setSize(w, rowH);
    m_userMenuAreas.push_back(areaPtr);
  }
}

void GreeterSurface::rebuildSessionMenu() {
  clearSessionMenu();
  if (!m_sessionMenuOpen || m_sessions.empty()) {
    return;
  }

  const float rowH = Style::controlHeightSm;
  const std::size_t count = m_sessions.size();
  const float x = m_sessionSelectBox->x();
  const float w = m_sessionSelectBox->width();
  const float y = m_sessionSelectBox->y() - (rowH * static_cast<float>(count)) -
                  Style::spaceXs;
  const float h = rowH * static_cast<float>(count);

  auto panel = std::make_unique<Box>();
  m_sessionMenuPanel = panel.get();
  m_sessionMenuPanel->setZIndex(50);
  m_root.addChild(std::move(panel));
  m_sessionMenuPanel->setPosition(x, y);
  m_sessionMenuPanel->setSize(w, h);
  m_sessionMenuPanel->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::SurfaceVariant),
      .border = colorForRole(ColorRole::Outline),
      .fillMode = FillMode::Solid,
      .radius = Style::scaledRadiusMd(),
      .softness = 1.0f,
      .borderWidth = Style::borderWidth,
  });

  for (std::size_t i = 0; i < count; ++i) {
    auto row = std::make_unique<Box>();
    auto *rowPtr = row.get();
    rowPtr->setZIndex(51);
    rowPtr->setPosition(x, y + rowH * static_cast<float>(i));
    rowPtr->setSize(w, rowH);
    rowPtr->setStyle(RoundedRectStyle{
        .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
        .fillMode = FillMode::Solid,
    });
    m_root.addChild(std::move(row));
    m_sessionMenuRows.push_back(rowPtr);

    auto label = std::make_unique<Label>();
    auto *labelPtr = label.get();
    labelPtr->setText(m_sessions[i].name);
    labelPtr->setFontSize(Style::fontSizeBody);
    labelPtr->setColor(i == m_selectedSession
                           ? colorForRole(ColorRole::Primary)
                           : colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(52);
    m_root.addChild(std::move(label));
    labelPtr->measure(*m_renderContext);
    labelPtr->setPosition(x + Style::spaceMd,
                          y + rowH * static_cast<float>(i) +
                              std::round((rowH - labelPtr->height()) * 0.5f));
    m_sessionMenuLabels.push_back(labelPtr);

    auto area = std::make_unique<InputArea>();
    auto *areaPtr = area.get();
    areaPtr->setFocusable(true);
    areaPtr->setZIndex(53);
    areaPtr->setOnEnter([this, i](const InputArea::PointerData &) {
      if (i < m_sessionMenuRows.size() && m_sessionMenuRows[i] != nullptr) {
        m_sessionMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::Hover, 0.35f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnLeave([this, i]() {
      if (i < m_sessionMenuRows.size() && m_sessionMenuRows[i] != nullptr) {
        m_sessionMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnClick([this, i](const InputArea::PointerData &data) {
      if (data.button != BTN_LEFT) {
        return;
      }
      selectSession(i);
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(x, y + rowH * static_cast<float>(i));
    areaPtr->setSize(w, rowH);
    m_sessionMenuAreas.push_back(areaPtr);
  }
}

void GreeterSurface::rebuildSchemeMenu() {
  clearSchemeMenu();
  if (!m_schemeMenuOpen || m_schemeNames.empty()) {
    return;
  }

  const float rowH = Style::controlHeightSm;
  const std::size_t count = m_schemeNames.size();
  const float x = m_schemeSelectBox->x();
  const float w = m_schemeSelectBox->width();
  const float y =
      m_schemeSelectBox->y() + m_schemeSelectBox->height() + Style::spaceXs;
  const float h = rowH * static_cast<float>(count);

  auto panel = std::make_unique<Box>();
  m_schemeMenuPanel = panel.get();
  m_schemeMenuPanel->setZIndex(60);
  m_root.addChild(std::move(panel));
  m_schemeMenuPanel->setPosition(x, y);
  m_schemeMenuPanel->setSize(w, h);
  m_schemeMenuPanel->setStyle(RoundedRectStyle{
      .fill = colorForRole(ColorRole::SurfaceVariant),
      .border = colorForRole(ColorRole::Outline),
      .fillMode = FillMode::Solid,
      .radius = Style::scaledRadiusMd(),
      .softness = 1.0f,
      .borderWidth = Style::borderWidth,
  });

  for (std::size_t i = 0; i < count; ++i) {
    auto row = std::make_unique<Box>();
    auto *rowPtr = row.get();
    rowPtr->setZIndex(61);
    rowPtr->setPosition(x, y + rowH * static_cast<float>(i));
    rowPtr->setSize(w, rowH);
    rowPtr->setStyle(RoundedRectStyle{
        .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
        .fillMode = FillMode::Solid,
    });
    m_root.addChild(std::move(row));
    m_schemeMenuRows.push_back(rowPtr);

    auto label = std::make_unique<Label>();
    auto *labelPtr = label.get();
    labelPtr->setText(m_schemeNames[i]);
    labelPtr->setFontSize(Style::fontSizeBody);
    labelPtr->setColor(i == m_selectedScheme
                           ? colorForRole(ColorRole::Primary)
                           : colorForRole(ColorRole::OnSurface));
    labelPtr->setZIndex(62);
    m_root.addChild(std::move(label));
    labelPtr->measure(*m_renderContext);
    labelPtr->setPosition(x + Style::spaceMd,
                          y + rowH * static_cast<float>(i) +
                              std::round((rowH - labelPtr->height()) * 0.5f));
    m_schemeMenuLabels.push_back(labelPtr);

    auto area = std::make_unique<InputArea>();
    auto *areaPtr = area.get();
    areaPtr->setFocusable(true);
    areaPtr->setZIndex(63);
    areaPtr->setOnEnter([this, i](const InputArea::PointerData &) {
      if (i < m_schemeMenuRows.size() && m_schemeMenuRows[i] != nullptr) {
        m_schemeMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::Hover, 0.35f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnLeave([this, i]() {
      if (i < m_schemeMenuRows.size() && m_schemeMenuRows[i] != nullptr) {
        m_schemeMenuRows[i]->setStyle(RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant, 0.01f),
            .fillMode = FillMode::Solid,
        });
      }
    });
    areaPtr->setOnClick([this, i](const InputArea::PointerData &data) {
      if (data.button != BTN_LEFT) {
        return;
      }
      selectScheme(i);
    });
    m_root.addChild(std::move(area));
    areaPtr->setPosition(x, y + rowH * static_cast<float>(i));
    areaPtr->setSize(w, rowH);
    m_schemeMenuAreas.push_back(areaPtr);
  }
}
