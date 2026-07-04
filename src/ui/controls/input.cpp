#include "ui/controls/input.h"

#include "core/key_modifiers.h"
#include "core/key_symbols.h"
#include "render/core/render_styles.h"
#include "render/scene/glyph_node.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {

  constexpr float kMinWidth = 48.0f;
  constexpr float kCursorWidth = 1.25f;
  constexpr float kCursorPadV = 4.0f;
  constexpr float kCursorMinHeight = 14.0f;
  constexpr float kCursorHeightRatio = 0.50f;
  constexpr float kCursorRevealPadding = 2.0f;
  constexpr float kTextInnerInset = 3.0f;
  constexpr float kPlaceholderAlpha = 0.68f;
  constexpr float kPasswordGlyphScale = 0.82f;
  constexpr float kRevealIconScale = 1.15f;

  Input::PasswordMaskStyle g_passwordMaskStyle = Input::PasswordMaskStyle::CircleFilled;

  char32_t passwordMaskCodepointForIndex(std::size_t index) {
    if (g_passwordMaskStyle == Input::PasswordMaskStyle::CircleFilled) {
      return GlyphRegistry::lookup("circle-filled");
    }
    static const std::array<char32_t, 7> randomCodepoints = {
        GlyphRegistry::lookup("circle-filled"),        GlyphRegistry::lookup("pentagon-filled"),
        GlyphRegistry::lookup("michelin-star-filled"), GlyphRegistry::lookup("square-rounded-filled"),
        GlyphRegistry::lookup("guitar-pick-filled"),   GlyphRegistry::lookup("blob-filled"),
        GlyphRegistry::lookup("triangle-filled"),
    };
    return randomCodepoints[index % randomCodepoints.size()];
  }

  void layoutPasswordMaskGlyph(
      Renderer& renderer, GlyphNode& glyph, char32_t codepoint, float glyphSize, float cellX, float cellSize,
      float inputHeight
  ) {
    glyph.setCodepoint(codepoint);
    glyph.setFontSize(glyphSize);
    glyph.setHitTestVisible(false);
    const auto metrics = renderer.measureGlyph(codepoint, glyphSize);
    const float cellCenterX = cellX + cellSize * 0.5f;
    const float emCenterX = glyphSize * 0.5f;
    const float inkCenterY = (metrics.top + metrics.bottom) * 0.5f;
    const float rowCenterY = inputHeight * 0.5f;
    glyph.setPosition(cellCenterX - emCenterX, rowCenterY - inkCenterY);
  }

} // namespace

void Input::setPasswordMaskStyle(PasswordMaskStyle style) noexcept { g_passwordMaskStyle = style; }

Input::Input() : Node(NodeType::Container) {
  auto bg = std::make_unique<RectNode>();
  m_background = static_cast<RectNode*>(addChild(std::move(bg)));

  auto textViewport = std::make_unique<Node>();
  textViewport->setClipChildren(true);
  textViewport->setHitTestVisible(false);
  m_textViewport = addChild(std::move(textViewport));

  auto sel = std::make_unique<RectNode>();
  sel->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Primary),
          .fillMode = FillMode::Solid,
          .radius = 2.0f,
      }
  );
  sel->setOpacity(0.3f);
  sel->setVisible(false);
  m_selectionRect = static_cast<RectNode*>(m_textViewport->addChild(std::move(sel)));

  auto label = std::make_unique<Label>();
  label->setFontSize(m_fontSize);
  label->setMaxLines(1);
  label->setColor(colorForRole(ColorRole::OnSurface));
  m_label = static_cast<Label*>(m_textViewport->addChild(std::move(label)));

  auto cursor = std::make_unique<RectNode>();
  cursor->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Primary),
          .fillMode = FillMode::Solid,
          .radius = 1.0f,
      }
  );
  cursor->setVisible(false);
  m_cursor = static_cast<RectNode*>(m_textViewport->addChild(std::move(cursor)));

  auto area = std::make_unique<InputArea>();
  area->setFocusable(true);
  area->setOnEnter([this](const InputArea::PointerData&) { applyVisualState(); });
  area->setOnLeave([this]() { applyVisualState(); });
  area->setOnPress([this](const InputArea::PointerData& data) {
    if (!data.pressed || data.button != BTN_LEFT) {
      return;
    }
    const float textStartX = (m_flatStyle ? kTextInnerInset : Style::spaceMd() + kTextInnerInset);
    m_cursorPos = xToByteOffset(data.localX - textStartX + m_scrollOffset);
    m_selectionAnchor = m_cursorPos;
    updateInteractiveGeometry();
    markLayoutDirty();
    markPaintDirty();
  });
  area->setOnKeyDown([this](const InputArea::KeyData& k) { handleKey(k.sym, k.utf32, k.modifiers, k.preedit); });
  area->setOnFocusChange([this](bool focused) {
    applyVisualState();
    updateInteractiveGeometry();
    if (!focused && m_onFocusLoss) {
      m_onFocusLoss();
    }
  });
  m_inputArea = static_cast<InputArea*>(addChild(std::move(area)));

  auto revealIcon = std::make_unique<GlyphNode>();
  revealIcon->setCodepoint(GlyphRegistry::lookup("eye"));
  revealIcon->setColor(colorForRole(ColorRole::OnSurfaceVariant));
  revealIcon->setHitTestVisible(false);
  revealIcon->setVisible(false);
  m_revealIcon = static_cast<GlyphNode*>(addChild(std::move(revealIcon)));

  auto revealArea = std::make_unique<InputArea>();
  revealArea->setOnPress([this](const InputArea::PointerData& data) {
    if (data.pressed && data.button == BTN_LEFT) {
      toggleReveal();
    }
  });
  m_revealArea = static_cast<InputArea*>(addChild(std::move(revealArea)));

  applyVisualState();
}

void Input::toggleReveal() {
  m_revealPassword = !m_revealPassword;
  m_revealIcon->setCodepoint(GlyphRegistry::lookup(m_revealPassword ? "eye-off" : "eye"));
  updateDisplayText();
  markLayoutDirty();
}

void Input::setValue(std::string_view value) {
  m_value = std::string(value);
  m_cursorPos = m_value.size();
  m_selectionAnchor = m_cursorPos;
  updateDisplayText();
  markLayoutDirty();
}

void Input::setPlaceholder(std::string_view placeholder) {
  m_placeholder = std::string(placeholder);
  if (m_value.empty()) {
    updateDisplayText();
    markLayoutDirty();
  }
}

void Input::setFontSize(float size) {
  m_fontSize = std::max(1.0f, size);
  if (m_label != nullptr) {
    m_label->setFontSize(m_fontSize);
  }
  markLayoutDirty();
}

void Input::setControlHeight(float height) { m_controlHeight = std::max(1.0f, height); }
void Input::setPasswordMode(bool enabled) {
  if (m_passwordMode == enabled) {
    return;
  }
  m_passwordMode = enabled;
  if (!m_passwordMode) {
    m_revealPassword = false;
    syncPasswordGlyphNodes(0);
  }
  updateDisplayText();
  markLayoutDirty();
}
void Input::setInvalid(bool invalid) {
  m_invalid = invalid;
  applyVisualState();
}
void Input::setEnabled(bool enabled) {
  m_enabled = enabled;
  if (m_inputArea) {
    m_inputArea->setEnabled(enabled);
  }
  setOpacity(enabled ? 1.0f : 0.55f);
}
void Input::setBold(bool) {}

void Input::setOnChange(std::function<void(const std::string&)> callback) { m_onChange = std::move(callback); }
void Input::setOnSubmit(std::function<void(const std::string&)> callback) { m_onSubmit = std::move(callback); }
void Input::setOnFocusLoss(std::function<void()> callback) { m_onFocusLoss = std::move(callback); }
void Input::setOnKeyDown(
    std::function<bool(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool preedit)> callback
) {
  m_onKeyDown = std::move(callback);
}

void Input::setEmbeddedStyle(bool embedded, float topRadius) {
  if (m_embeddedStyle == embedded && m_embeddedTopRadius == topRadius) {
    return;
  }
  m_embeddedStyle = embedded;
  m_embeddedTopRadius = topRadius;
  applyVisualState();
}

void Input::setFlatStyle(bool flat) {
  if (m_flatStyle == flat) {
    return;
  }
  m_flatStyle = flat;
  if (m_flatStyle) {
    m_embeddedStyle = false;
  }
  applyVisualState();
  markLayoutDirty();
}

void Input::setFlatOnSecondary(bool onSecondary) {
  if (m_flatOnSecondary == onSecondary) {
    return;
  }
  m_flatOnSecondary = onSecondary;
  applyVisualState();
}

void Input::selectAll() {
  m_selectionAnchor = 0;
  m_cursorPos = m_value.size();
  updateInteractiveGeometry();
  markPaintDirty();
}

void Input::updateDisplayText() {
  if (m_label == nullptr) {
    return;
  }
  if (m_value.empty() && !m_placeholder.empty()) {
    m_label->setText(m_placeholder);
  } else {
    m_label->setText(m_passwordMode && !m_revealPassword ? std::string{} : m_value);
  }
}

void Input::handleKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool preedit) {
  if (m_onKeyDown && m_onKeyDown(sym, utf32, modifiers, preedit)) {
    return;
  }
  if (preedit) {
    return;
  }

  const bool shift = (modifiers & KeyMod::Shift) != 0;
  const bool ctrl = (modifiers & KeyMod::Ctrl) != 0;

  if (utf32 == 0) {
    const bool navigationOrEdit = KeySymbol::isBackspace(sym)
        || KeySymbol::isDelete(sym)
        || KeySymbol::isLeft(sym)
        || KeySymbol::isRight(sym)
        || KeySymbol::isHome(sym)
        || KeySymbol::isEnd(sym)
        || KeySymbol::isEnter(sym)
        || (ctrl && (sym == 'a' || sym == 'A' || sym == XKB_KEY_a || sym == XKB_KEY_A));
    if (!navigationOrEdit) {
      return;
    }
  }

  bool changed = false;

  if (KeySymbol::isEnter(sym)) {
    if (m_onSubmit) {
      m_onSubmit(m_value);
    }
    return;
  }

  if (ctrl && (sym == 'a' || sym == 'A' || sym == XKB_KEY_a || sym == XKB_KEY_A)) {
    m_selectionAnchor = 0;
    m_cursorPos = m_value.size();
    updateInteractiveGeometry();
    markPaintDirty();
    return;
  } else if (KeySymbol::isBackspace(sym)) {
    if (hasSelection()) {
      deleteSelection();
      changed = true;
    } else if (m_cursorPos > 0) {
      const std::size_t prev = prevCharPos(m_value, m_cursorPos);
      m_value.erase(prev, m_cursorPos - prev);
      m_cursorPos = prev;
      m_selectionAnchor = prev;
      changed = true;
    }
  } else if (KeySymbol::isDelete(sym)) {
    if (hasSelection()) {
      deleteSelection();
      changed = true;
    } else if (m_cursorPos < m_value.size()) {
      const std::size_t next = nextCharPos(m_value, m_cursorPos);
      m_value.erase(m_cursorPos, next - m_cursorPos);
      changed = true;
    }
  } else if (KeySymbol::isLeft(sym)) {
    if (!shift && hasSelection()) {
      m_cursorPos = selectionStart();
      m_selectionAnchor = m_cursorPos;
    } else {
      m_cursorPos = prevCharPos(m_value, m_cursorPos);
      if (!shift) {
        m_selectionAnchor = m_cursorPos;
      }
    }
  } else if (KeySymbol::isRight(sym)) {
    if (!shift && hasSelection()) {
      m_cursorPos = selectionEnd();
      m_selectionAnchor = m_cursorPos;
    } else {
      m_cursorPos = nextCharPos(m_value, m_cursorPos);
      if (!shift) {
        m_selectionAnchor = m_cursorPos;
      }
    }
  } else if (KeySymbol::isHome(sym)) {
    m_cursorPos = 0;
    if (!shift) {
      m_selectionAnchor = 0;
    }
  } else if (KeySymbol::isEnd(sym)) {
    m_cursorPos = m_value.size();
    if (!shift) {
      m_selectionAnchor = m_cursorPos;
    }
  } else if (utf32 >= 0x20U && utf32 != 0x7FU) {
    if (hasSelection()) {
      deleteSelection();
      changed = true;
    }
    const auto bytes = utf32ToUtf8(utf32);
    m_value.insert(m_cursorPos, bytes);
    m_cursorPos += bytes.size();
    m_selectionAnchor = m_cursorPos;
    changed = true;
  }

  updateDisplayText();
  markLayoutDirty();
  if (m_inputArea != nullptr && m_inputArea->focused()) {
    updateInteractiveGeometry();
    if (m_cursor != nullptr) {
      m_cursor->setVisible(true);
    }
  }

  if (changed && m_onChange) {
    m_onChange(m_value);
  }
}

void Input::doLayout(Renderer& renderer) {
  const float w = width() > 0.0f ? width() : kMinWidth;
  const float h = m_controlHeight;
  setSize(w, h);

  const bool showPasswordGlyphs = m_passwordMode && !m_revealPassword && !m_value.empty();
  m_label->setVisible(!showPasswordGlyphs);
  if (!showPasswordGlyphs) {
    const bool showingPlaceholder = m_value.empty() && !m_placeholder.empty();
    if (showingPlaceholder) {
      const float textInset = m_flatStyle ? kTextInnerInset : Style::spaceMd() + kTextInnerInset;
      m_label->setMaxWidth(std::max(0.0f, w - textInset * 2.0f - revealSlotWidth()));
    } else {
      m_label->setMaxWidth(0.0f);
    }
    m_label->measure(renderer);
  }

  m_stopByte.clear();
  m_stopX.clear();
  m_stopByte.push_back(0);
  m_stopX.push_back(0.0f);

  std::size_t charCount = 0;
  float passwordGlyphSize = 0.0f;
  const float passwordCellSize = showPasswordGlyphs ? std::round(m_fontSize * kPasswordGlyphScale) : 0.0f;
  if (showPasswordGlyphs) {
    passwordGlyphSize = m_fontSize * kPasswordGlyphScale;
  }

  if (!m_value.empty()) {
    std::size_t pos = 0;
    float maskX = 0.0f;
    while (pos < m_value.size()) {
      pos = nextCharPos(m_value, pos);
      ++charCount;
      m_stopByte.push_back(pos);
      if (showPasswordGlyphs) {
        maskX += passwordCellSize;
        m_stopX.push_back(maskX);
      }
    }
    if (!showPasswordGlyphs) {
      renderer.measureTextCursorStops(m_value, m_fontSize, m_stopByte, m_stopX);
      if (m_stopX.size() != m_stopByte.size()) {
        m_stopX.assign(m_stopByte.size(), 0.0f);
      }
    }
  }

  if (m_inputArea != nullptr && m_inputArea->focused()) {
    ensureCursorVisible();
  } else {
    m_scrollOffset = 0.0f;
  }

  if (showPasswordGlyphs) {
    syncPasswordGlyphNodes(charCount);
    float gx = -m_scrollOffset;
    for (std::size_t i = 0; i < m_passwordGlyphs.size(); ++i) {
      auto* glyph = m_passwordGlyphs[i];
      layoutPasswordMaskGlyph(
          renderer, *glyph, passwordMaskCodepointForIndex(i), passwordGlyphSize, gx, passwordCellSize, h
      );
      glyph->setColor(colorForRole(ColorRole::OnSurface));
      glyph->setVisible(true);
      gx += passwordCellSize;
    }
  } else {
    syncPasswordGlyphNodes(0);
    const float labelY = std::round((h - m_label->height()) * 0.5f);
    m_label->setPosition(-m_scrollOffset, labelY);
  }

  if (m_background != nullptr) {
    m_background->setPosition(0.0f, 0.0f);
    m_background->setSize(w, h);
  }

  if (m_textViewport != nullptr) {
    const float textInset = m_flatStyle ? kTextInnerInset : Style::spaceMd() + kTextInnerInset;
    const float viewportW = std::max(0.0f, w - textInset * 2.0f - revealSlotWidth());
    m_textViewport->setPosition(textInset, 0.0f);
    m_textViewport->setSize(viewportW, h);
  }

  if (m_inputArea != nullptr) {
    m_inputArea->setPosition(0.0f, 0.0f);
    m_inputArea->setSize(w, h);
  }

  const bool showReveal = m_passwordMode && !m_value.empty();
  if (m_revealIcon != nullptr && m_revealArea != nullptr) {
    m_revealIcon->setVisible(showReveal);
    m_revealArea->setEnabled(showReveal);
    if (showReveal) {
      const float iconSize = std::round(m_fontSize * kRevealIconScale);
      const float slot = revealSlotWidth();
      const auto metrics = renderer.measureGlyph(m_revealIcon->codepoint(), iconSize);
      const float iconX = w - Style::spaceMd() - iconSize;
      m_revealIcon->setFontSize(iconSize);
      m_revealIcon->setPosition(iconX, std::round((h - (metrics.top + metrics.bottom)) * 0.5f));
      m_revealArea->setPosition(w - slot, 0.0f);
      m_revealArea->setSize(slot, h);
    }
  }

  updateInteractiveGeometry();
  applyVisualState();
}

LayoutSize Input::doMeasure(Renderer& renderer, const LayoutConstraints& constraints) {
  doLayout(renderer);
  return constraints.constrain({width(), height()});
}

void Input::applyVisualState() {
  if (m_background == nullptr) {
    return;
  }

  const bool focused = m_inputArea != nullptr && m_inputArea->focused();
  const bool hovered = m_inputArea != nullptr && m_inputArea->hovered();

  if (m_flatStyle) {
    m_background->setStyle(
        RoundedRectStyle{
            .fillMode = FillMode::None,
            .radius = Radii(0.0f),
            .borderWidth = 0.0f,
        }
    );
    const bool showingPlaceholder = m_value.empty() && !m_placeholder.empty();
    const Color textColor = m_invalid ? colorForRole(ColorRole::Error)
        : showingPlaceholder
        ? colorForRole(m_flatOnSecondary ? ColorRole::OnSecondary : ColorRole::OnSurfaceVariant, kPlaceholderAlpha)
        : m_flatOnSecondary ? colorForRole(ColorRole::OnSecondary)
                            : colorForRole(ColorRole::OnSurface);
    if (m_label != nullptr) {
      m_label->setColor(textColor);
    }
    for (auto* glyph : m_passwordGlyphs) {
      glyph->setColor(textColor);
    }
    return;
  }

  Color fill = focused ? colorForRole(ColorRole::Surface) : colorForRole(ColorRole::SurfaceVariant);
  Color border = m_invalid ? colorForRole(ColorRole::Error)
      : focused            ? colorForRole(ColorRole::Primary)
      : hovered            ? colorForRole(ColorRole::Hover)
                           : colorForRole(ColorRole::Outline);
  Radii radius(Style::scaledRadiusMd());
  float borderWidth = Style::borderWidth();

  if (m_embeddedStyle) {
    fill = colorForRole(ColorRole::SurfaceVariant);
    border = focused ? colorForRole(ColorRole::Primary) : Color{};
    borderWidth = focused ? Style::borderWidth() : 0.0f;
    const float topRadius = std::max(0.0f, m_embeddedTopRadius);
    radius = Radii(topRadius, topRadius, 0.0f, 0.0f);
  }

  m_background->setStyle(
      RoundedRectStyle{
          .fill = fill,
          .border = border,
          .fillMode = FillMode::Solid,
          .radius = radius,
          .softness = 1.0f,
          .borderWidth = borderWidth,
      }
  );

  const bool showingPlaceholder = m_value.empty() && !m_placeholder.empty();
  const Color textColor = m_invalid ? colorForRole(ColorRole::Error)
      : showingPlaceholder          ? colorForRole(ColorRole::OnSurfaceVariant, kPlaceholderAlpha)
                                    : colorForRole(ColorRole::OnSurface);
  if (m_label != nullptr) {
    m_label->setColor(textColor);
  }
  for (auto* glyph : m_passwordGlyphs) {
    glyph->setColor(textColor);
  }
}

void Input::updateInteractiveGeometry() {
  if (m_cursor == nullptr || m_selectionRect == nullptr) {
    return;
  }

  const float previousScrollOffset = m_scrollOffset;
  if (m_inputArea != nullptr && (m_inputArea->focused() || hasSelection())) {
    ensureCursorVisible();
  } else {
    m_scrollOffset = 0.0f;
  }
  if (std::abs(m_scrollOffset - previousScrollOffset) > 0.001f) {
    markLayoutDirty();
  }

  const float controlHeight = height() > 0.0f ? height() : m_controlHeight;
  const float maxCursorHeight = std::max(0.0f, controlHeight - kCursorPadV * 2.0f);
  const float cursorHeight =
      std::clamp(controlHeight * kCursorHeightRatio, std::min(kCursorMinHeight, maxCursorHeight), maxCursorHeight);
  const float cursorY = std::round((controlHeight - cursorHeight) * 0.5f);
  const float cursorX = stopXForByte(m_cursorPos) - m_scrollOffset;
  m_cursor->setPosition(cursorX, cursorY);
  m_cursor->setSize(kCursorWidth, cursorHeight);

  if (hasSelection()) {
    const float selX0 = stopXForByte(selectionStart()) - m_scrollOffset;
    const float selX1 = stopXForByte(selectionEnd()) - m_scrollOffset;
    m_selectionRect->setPosition(selX0, cursorY);
    m_selectionRect->setSize(std::max(0.0f, selX1 - selX0), cursorHeight);
    if (m_flatStyle) {
      m_selectionRect->setStyle(
          RoundedRectStyle{
              .fill = colorForRole(ColorRole::Primary),
              .fillMode = FillMode::Solid,
              .radius = 2.0f,
          }
      );
      m_selectionRect->setOpacity(0.35f);
    }
    m_selectionRect->setVisible(true);
    const bool fullSelection = selectionStart() == 0 && selectionEnd() == m_value.size() && !m_value.empty();
    m_cursor->setVisible(m_inputArea != nullptr && m_inputArea->focused() && !fullSelection);
  } else {
    m_selectionRect->setVisible(false);
    m_cursor->setVisible(m_inputArea != nullptr && m_inputArea->focused());
  }
}

void Input::ensureCursorVisible() {
  if (m_value.empty() || m_stopX.empty()) {
    m_scrollOffset = 0.0f;
    return;
  }

  const float viewportWidth = textViewportWidth();
  if (viewportWidth <= 0.0f) {
    m_scrollOffset = 0.0f;
    return;
  }

  const float cursorContentX = stopXForByte(m_cursorPos);
  const float revealPad = std::max(kCursorRevealPadding, kTextInnerInset);
  const float cursorVx = cursorContentX - m_scrollOffset;
  const float leftEdge = revealPad;
  const float rightEdge = viewportWidth - revealPad - kCursorWidth;

  if (cursorVx < leftEdge) {
    m_scrollOffset = cursorContentX - leftEdge;
  } else if (cursorVx > rightEdge) {
    m_scrollOffset = cursorContentX - rightEdge;
  }

  clampScrollOffset();
}

void Input::clampScrollOffset() {
  if (m_value.empty() || m_stopX.empty()) {
    m_scrollOffset = 0.0f;
    return;
  }
  const float maxOffset = std::max(0.0f, m_stopX.back() - textViewportWidth() + kCursorWidth + kCursorRevealPadding);
  m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxOffset);
}

void Input::syncPasswordGlyphNodes(std::size_t count) {
  if (m_textViewport == nullptr) {
    return;
  }
  while (m_passwordGlyphs.size() > count) {
    auto* node = m_passwordGlyphs.back();
    (void)m_textViewport->removeChild(node);
    m_passwordGlyphs.pop_back();
  }
  while (m_passwordGlyphs.size() < count) {
    auto glyph = std::make_unique<GlyphNode>();
    auto* glyphPtr = static_cast<GlyphNode*>(m_textViewport->insertChildAt(2, std::move(glyph)));
    m_passwordGlyphs.push_back(glyphPtr);
  }
}

void Input::deleteSelection() {
  const std::size_t start = selectionStart();
  const std::size_t end = selectionEnd();
  m_value.erase(start, end - start);
  m_cursorPos = start;
  m_selectionAnchor = start;
}

bool Input::hasSelection() const noexcept { return m_selectionAnchor != m_cursorPos; }

std::size_t Input::selectionStart() const noexcept { return std::min(m_selectionAnchor, m_cursorPos); }

std::size_t Input::selectionEnd() const noexcept { return std::max(m_selectionAnchor, m_cursorPos); }

float Input::textViewportWidth() const noexcept {
  const float w = width() > 0.0f ? width() : kMinWidth;
  const float textInset = m_flatStyle ? kTextInnerInset : Style::spaceMd() + kTextInnerInset;
  return std::max(0.0f, w - textInset * 2.0f - revealSlotWidth());
}

float Input::revealSlotWidth() const noexcept {
  if (!m_passwordMode || m_value.empty()) {
    return 0.0f;
  }
  return std::round(m_fontSize * kRevealIconScale) + Style::spaceMd() + Style::spaceSm();
}

float Input::stopXForByte(std::size_t bytePos) const {
  for (std::size_t i = 0; i < m_stopByte.size(); ++i) {
    if (m_stopByte[i] == bytePos) {
      return m_stopX[i];
    }
  }
  return m_stopX.empty() ? 0.0f : m_stopX.back();
}

std::size_t Input::xToByteOffset(float localX) const {
  if (m_stopX.empty() || localX <= 0.0f) {
    return 0;
  }
  if (localX >= m_stopX.back()) {
    return m_stopByte.back();
  }
  for (std::size_t i = 1; i < m_stopX.size(); ++i) {
    const float mid = (m_stopX[i - 1] + m_stopX[i]) * 0.5f;
    if (localX < mid) {
      return m_stopByte[i - 1];
    }
  }
  return m_stopByte.back();
}

std::size_t Input::nextCharPos(const std::string& s, std::size_t pos) {
  if (pos >= s.size()) {
    return pos;
  }
  ++pos;
  while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0U) == 0x80U) {
    ++pos;
  }
  return pos;
}

std::size_t Input::prevCharPos(const std::string& s, std::size_t pos) {
  if (pos == 0) {
    return 0;
  }
  --pos;
  while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0U) == 0x80U) {
    --pos;
  }
  return pos;
}

std::string Input::utf32ToUtf8(std::uint32_t cp) {
  std::string result;
  if (cp < 0x80U) {
    result += static_cast<char>(cp);
  } else if (cp < 0x800U) {
    result += static_cast<char>(0xC0U | (cp >> 6U));
    result += static_cast<char>(0x80U | (cp & 0x3FU));
  } else if (cp < 0x10000U) {
    result += static_cast<char>(0xE0U | (cp >> 12U));
    result += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    result += static_cast<char>(0x80U | (cp & 0x3FU));
  } else {
    result += static_cast<char>(0xF0U | (cp >> 18U));
    result += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
    result += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    result += static_cast<char>(0x80U | (cp & 0x3FU));
  }
  return result;
}
