#include "ui/controls/label.h"

#include "render/scene/text_node.h"
#include "ui/palette.h"
#include "ui/style.h"

Label::Label() : InputArea() {
  auto textNode = std::make_unique<TextNode>();
  textNode->setFontSize(Style::fontSizeBody());
  textNode->setColor(colorForRole(ColorRole::OnSurface));
  m_textNode = static_cast<TextNode*>(addChild(std::move(textNode)));
  setHitTestVisible(false);
}

bool Label::setText(std::string_view text) {
  if (m_plainText == text)
    return false;
  m_plainText = std::string(text);
  m_textNode->setText(m_plainText);
  m_measureCached = false;
  return true;
}

void Label::setFontSize(float size) {
  if (m_textNode->fontSize() == size)
    return;
  m_textNode->setFontSize(size);
  m_measureCached = false;
}

void Label::setFontFamily(std::string family) {
  if (m_textNode->fontFamily() == family)
    return;
  m_textNode->setFontFamily(std::move(family));
  m_measureCached = false;
}

void Label::setColor(const Color& color) {
  m_color = color;
  m_textNode->setColor(m_color);
}

void Label::setMinWidth(float minWidth) {
  if (m_minWidth == minWidth)
    return;
  m_minWidth = minWidth;
  m_measureCached = false;
}

void Label::setMaxWidth(float maxWidth) {
  if (m_userMaxWidth == maxWidth)
    return;
  m_userMaxWidth = maxWidth;
  m_textNode->setMaxWidth(maxWidth);
  m_measureCached = false;
}

void Label::setMaxLines(int maxLines) {
  if (m_userMaxLines == maxLines)
    return;
  m_userMaxLines = maxLines;
  m_textNode->setMaxLines(maxLines);
  m_measureCached = false;
}

void Label::setBold(bool bold) {
  if (m_textNode->bold() == bold)
    return;
  m_textNode->setBold(bold);
  m_measureCached = false;
}

void Label::setTextAlign(TextAlign align) {
  if (m_textNode->textAlign() == align)
    return;
  m_textNode->setTextAlign(align);
  m_measureCached = false;
}

float Label::fontSize() const noexcept { return m_textNode->fontSize(); }
const Color& Label::color() const noexcept { return m_textNode->color(); }
bool Label::bold() const noexcept { return m_textNode->bold(); }
TextAlign Label::textAlign() const noexcept { return m_textNode->textAlign(); }

void Label::measure(Renderer& renderer) {
  LayoutConstraints c;
  measureWithConstraints(renderer, c);
}

LayoutSize Label::measureWithConstraints(Renderer& renderer, const LayoutConstraints& constraints) {
  float measureMaxWidth = m_userMaxWidth;
  if (constraints.hasMaxWidth) {
    measureMaxWidth = measureMaxWidth > 0 ? std::min(measureMaxWidth, constraints.maxWidth) : constraints.maxWidth;
  }

  const bool singleLine = m_userMaxLines == 1
      || (m_userMaxLines == 0 && measureMaxWidth <= 0 && m_plainText.find('\n') == std::string::npos);

  if (m_measureCached
      && m_cachedText == m_plainText
      && m_cachedFontSize == m_textNode->fontSize()
      && m_cachedBold == m_textNode->bold()
      && m_cachedMaxWidth == m_userMaxWidth
      && m_cachedMaxLines == m_userMaxLines) {
    return {width(), height()};
  }

  auto metrics = renderer.measureText(
      m_plainText, m_textNode->fontSize(), m_textNode->bold(), measureMaxWidth, m_userMaxLines, m_textNode->textAlign(),
      m_textNode->fontFamily()
  );

  float measuredWidth = measureMaxWidth > 0 ? std::min(metrics.width, measureMaxWidth) : metrics.width;
  float actualHeight = metrics.bottom - metrics.top;
  float inkHeight = std::max(0.0f, metrics.inkBottom - metrics.inkTop);

  float h = 0;
  if (singleLine && inkHeight > 0) {
    h = std::round(std::max(actualHeight, inkHeight));
  } else {
    h = std::round(actualHeight);
  }

  float w =
      constraints.hasExactWidth() ? std::max(constraints.maxWidth, m_minWidth) : std::max(measuredWidth, m_minWidth);
  setSize(std::round(w), h);

  // Position text node
  float textX = 0;
  if (m_textNode->textAlign() == TextAlign::Center && measuredWidth < width()) {
    textX = (width() - measuredWidth) * 0.5f;
  } else if (m_textNode->textAlign() == TextAlign::End && measuredWidth < width()) {
    textX = width() - measuredWidth;
  }
  m_textNode->setPosition(textX, -metrics.top);

  m_cachedText = m_plainText;
  m_cachedFontSize = m_textNode->fontSize();
  m_cachedBold = m_textNode->bold();
  m_cachedMaxWidth = m_userMaxWidth;
  m_cachedMaxLines = m_userMaxLines;
  m_measureCached = true;

  return {width(), height()};
}

LayoutSize Label::doMeasure(Renderer& renderer, const LayoutConstraints& constraints) {
  return measureWithConstraints(renderer, constraints);
}

void Label::doLayout(Renderer& renderer) { measure(renderer); }
