#include "ui/controls/flex.h"

#include "render/core/render_styles.h"
#include "render/scene/rect_node.h"

#include <algorithm>
#include <memory>

Flex::Flex() : Node(NodeType::Container) {}

void Flex::setDirection(FlexDirection direction) { m_direction = direction; }
void Flex::setAlign(FlexAlign align) { m_align = align; }
void Flex::setGap(float gap) { m_gap = std::max(0.0f, gap); }
void Flex::setPadding(float padding) { m_paddingLeft = m_paddingRight = m_paddingTop = m_paddingBottom = padding; }
void Flex::setPadding(float horizontal, float vertical) {
  m_paddingLeft = m_paddingRight = horizontal;
  m_paddingTop = m_paddingBottom = vertical;
}
void Flex::setPadding(float left, float top, float right, float bottom) {
  m_paddingLeft = left;
  m_paddingTop = top;
  m_paddingRight = right;
  m_paddingBottom = bottom;
}
void Flex::setRadius(float radius) {
  m_radius = std::max(0.0f, radius);
  syncBackgroundStyle();
}
void Flex::setFill(const Color& fill) {
  m_fill = fill;
  ensureBackground();
  syncBackgroundStyle();
}
void Flex::setBorder(const Color& border, float width) {
  m_border = border;
  m_borderWidth = std::max(0.0f, width);
  ensureBackground();
  syncBackgroundStyle();
}

void Flex::ensureBackground() {
  if (m_background != nullptr) {
    return;
  }
  auto rect = std::make_unique<RectNode>();
  m_background = static_cast<RectNode*>(addChild(std::move(rect)));
  m_background->setZIndex(-1);
  m_background->setHitTestVisible(false);
  m_background->setFrameSize(width(), height());
  syncBackgroundStyle();
}

void Flex::syncBackgroundStyle() {
  if (m_background == nullptr) {
    return;
  }
  m_background->setStyle(
      RoundedRectStyle{
          .fill = m_fill,
          .border = m_border,
          .fillMode = FillMode::Solid,
          .radius = m_radius,
          .softness = 1.0f,
          .borderWidth = m_borderWidth,
      }
  );
}
void Flex::setMinWidth(float width) { m_minWidth = std::max(0.0f, width); }
void Flex::setMinHeight(float height) { m_minHeight = std::max(0.0f, height); }

LayoutSize Flex::doMeasure(Renderer& renderer, const LayoutConstraints& constraints) {
  float contentWidth = 0;
  float contentHeight = 0;
  float totalGap = 0;
  int visibleCount = 0;

  for (const auto& child : m_children) {
    if (!child->visible() || !child->participatesInLayout() || child.get() == m_background)
      continue;
    ++visibleCount;

    if (m_direction == FlexDirection::Horizontal) {
      LayoutConstraints childConstraints;
      if (constraints.hasMaxWidth) {
        childConstraints.setMaxWidth(std::max(0.0f, constraints.maxWidth - m_paddingLeft - m_paddingRight - totalGap));
      }
      LayoutSize childSize = child->measure(renderer, childConstraints);
      contentWidth += childSize.width;
      contentHeight = std::max(contentHeight, childSize.height);
      totalGap += m_gap;
    } else {
      LayoutConstraints childConstraints;
      if (constraints.hasMaxHeight) {
        childConstraints.setMaxHeight(
            std::max(0.0f, constraints.maxHeight - m_paddingTop - m_paddingBottom - totalGap)
        );
      }
      LayoutSize childSize = child->measure(renderer, childConstraints);
      contentWidth = std::max(contentWidth, childSize.width);
      contentHeight += childSize.height;
      totalGap += m_gap;
    }
  }

  if (visibleCount > 0) {
    totalGap -= m_gap; // Remove last gap
  }

  float w = m_paddingLeft + m_paddingRight + contentWidth + totalGap;
  float h = m_paddingTop + m_paddingBottom + contentHeight + totalGap;
  w = std::max(w, m_minWidth);
  h = std::max(h, m_minHeight);

  return constraints.constrain({w, h});
}

void Flex::doLayout(Renderer& renderer) {
  if (m_background != nullptr) {
    m_background->setPosition(0.0f, 0.0f);
    m_background->setSize(width(), height());
  }

  // Ensure child controls have up-to-date intrinsic sizes before positioning.
  for (const auto& child : m_children) {
    if (!child->visible() || !child->participatesInLayout() || child.get() == m_background) {
      continue;
    }
    (void)child->measure(renderer, LayoutConstraints::unconstrained());
  }

  float pos = 0;
  float mainSize = (m_direction == FlexDirection::Horizontal) ? width() : height();
  float crossSize = (m_direction == FlexDirection::Horizontal) ? height() : width();

  // Calculate total content size for alignment
  float totalContentSize = 0;
  int visibleCount = 0;
  for (const auto& child : m_children) {
    if (!child->visible() || !child->participatesInLayout() || child.get() == m_background)
      continue;
    ++visibleCount;
    if (m_direction == FlexDirection::Horizontal) {
      totalContentSize += child->width();
    } else {
      totalContentSize += child->height();
    }
    if (visibleCount > 1)
      totalContentSize += m_gap;
  }

  // Apply alignment
  float offset = 0;
  if (m_align == FlexAlign::Center) {
    offset = (mainSize - totalContentSize) * 0.5f;
  } else if (m_align == FlexAlign::End) {
    offset = mainSize - totalContentSize;
  }
  offset = std::max(0.0f, offset);

  pos = (m_direction == FlexDirection::Horizontal) ? m_paddingLeft : m_paddingTop;
  pos += offset;

  for (const auto& child : m_children) {
    if (!child->visible() || !child->participatesInLayout() || child.get() == m_background)
      continue;

    float childMain = (m_direction == FlexDirection::Horizontal) ? child->width() : child->height();
    float childCross = (m_direction == FlexDirection::Horizontal) ? child->height() : child->width();

    float crossPos = 0;
    if (m_align == FlexAlign::Center) {
      crossPos = (crossSize - childCross) * 0.5f;
    } else if (m_align == FlexAlign::End) {
      crossPos = crossSize - childCross;
    }

    if (m_direction == FlexDirection::Horizontal) {
      child->setPosition(pos, crossPos + m_paddingTop);
    } else {
      child->setPosition(crossPos + m_paddingLeft, pos);
    }

    child->layout(renderer);
    pos += childMain + m_gap;
  }
}
