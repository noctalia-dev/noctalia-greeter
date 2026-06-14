#include "render/scene/node.h"

#include "render/animation/animation_manager.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

  Mat3 localTransform(const Node* node) {
    float cx = node->width() * 0.5f;
    float cy = node->height() * 0.5f;
    return Mat3::translation(node->x(), node->y())
        * Mat3::translation(cx, cy)
        * Mat3::rotation(node->rotation())
        * Mat3::scale(node->scale(), node->scale())
        * Mat3::translation(-cx, -cy);
  }

  Mat3 computeWorldTransform(const Node* node) {
    Mat3 world = Mat3::identity();
    for (const Node* current = node; current != nullptr; current = current->parent()) {
      world = localTransform(current) * world;
    }
    return world;
  }

  bool pointInsideNode(const Node* node, float sceneX, float sceneY, float& localX, float& localY) {
    if (node == nullptr)
      return false;

    Mat3 inverse = computeWorldTransform(node).inverse();
    Vec2 local = inverse.transformPoint(sceneX, sceneY);
    localX = local.x;
    localY = local.y;
    return localX >= 0.0f && localX < node->width() && localY >= 0.0f && localY < node->height();
  }

} // namespace

LayoutConstraints LayoutConstraints::unconstrained() noexcept { return {}; }
LayoutConstraints LayoutConstraints::available(float width, float height) noexcept {
  LayoutConstraints c;
  c.setMaxWidth(width);
  c.setMaxHeight(height);
  return c;
}
LayoutConstraints LayoutConstraints::exact(float width, float height) noexcept {
  LayoutConstraints c;
  c.setExactWidth(width);
  c.setExactHeight(height);
  return c;
}

void LayoutConstraints::setMaxWidth(float width) noexcept {
  maxWidth = std::max(0.0f, width);
  hasMaxWidth = true;
}
void LayoutConstraints::setMaxHeight(float height) noexcept {
  maxHeight = std::max(0.0f, height);
  hasMaxHeight = true;
}
void LayoutConstraints::setExactWidth(float width) noexcept {
  minWidth = std::max(0.0f, width);
  setMaxWidth(minWidth);
}
void LayoutConstraints::setExactHeight(float height) noexcept {
  minHeight = std::max(0.0f, height);
  setMaxHeight(minHeight);
}
bool LayoutConstraints::hasExactWidth() const noexcept { return hasMaxWidth && minWidth == maxWidth; }
bool LayoutConstraints::hasExactHeight() const noexcept { return hasMaxHeight && minHeight == maxHeight; }

float LayoutConstraints::constrainWidth(float width) const noexcept {
  float c = std::max(width, minWidth);
  if (hasMaxWidth)
    c = std::min(c, maxWidth);
  return c;
}
float LayoutConstraints::constrainHeight(float height) const noexcept {
  float c = std::max(height, minHeight);
  if (hasMaxHeight)
    c = std::min(c, maxHeight);
  return c;
}
LayoutSize LayoutConstraints::constrain(LayoutSize size) const noexcept {
  return {constrainWidth(size.width), constrainHeight(size.height)};
}

Node::Node(NodeType type) : m_type(type) {}

Node::~Node() {
  if (m_animationManager != nullptr) {
    m_animationManager->cancelForOwner(this);
  }
}

void Node::layout(Renderer& renderer) { doLayout(renderer); }

LayoutSize Node::measure(Renderer& renderer, const LayoutConstraints& constraints) {
  return doMeasure(renderer, constraints);
}

void Node::arrange(Renderer& renderer, const LayoutRect& rect) {
  m_arranging = true;
  m_sizeAssignedByLayout = true;
  doArrange(renderer, rect);
  m_arranging = false;
}

bool Node::containsScenePoint(float sceneX, float sceneY) const {
  float localX = 0, localY = 0;
  return pointInsideNode(this, sceneX, sceneY, localX, localY);
}

Node* Node::hitTest(Node* root, float x, float y) { return hitTestImpl(root, x, y); }

Node* Node::hitTestImpl(Node* node, float px, float py) {
  if (node == nullptr || !node->m_visible || !node->m_hitTestVisible)
    return nullptr;

  float localX = 0, localY = 0;
  bool inside = pointInsideNode(node, px, py, localX, localY);

  if (node->m_clipChildren && !inside)
    return nullptr;

  auto& children = node->m_children;
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    Node* hit = hitTestImpl(it->get(), px, py);
    if (hit != nullptr)
      return hit;
  }

  return inside ? node : nullptr;
}

void Node::doLayout(Renderer& renderer) {
  for (auto& child : m_children) {
    if (child->visible())
      child->layout(renderer);
  }
}

LayoutSize Node::doMeasure(Renderer& renderer, const LayoutConstraints& constraints) {
  doLayout(renderer);
  return constraints.constrain({width(), height()});
}

void Node::doArrange(Renderer& renderer, const LayoutRect& rect) {
  setPosition(rect.x, rect.y);
  setSize(rect.width, rect.height);
  doLayout(renderer);
}

LayoutSize Node::measureByLayout(Renderer& renderer, const LayoutConstraints& /*constraints*/) {
  doLayout(renderer);
  return {width(), height()};
}

void Node::arrangeByLayout(Renderer& renderer, const LayoutRect& rect) {
  setPosition(rect.x, rect.y);
  setSize(rect.width, rect.height);
  doLayout(renderer);
}

void Node::setPosition(float x, float y) {
  if (m_x == x && m_y == y)
    return;
  m_x = x;
  m_y = y;
  markPaintDirty();
}

void Node::setSize(float width, float height) {
  if (!m_arranging)
    m_sizeAssignedByLayout = false;
  if (m_width == width && m_height == height)
    return;
  m_width = width;
  m_height = height;
  markLayoutDirty();
}

void Node::setFrameSize(float width, float height) {
  if (!m_arranging)
    m_sizeAssignedByLayout = false;
  if (m_width == width && m_height == height)
    return;
  m_width = width;
  m_height = height;
  markPaintDirty();
}

void Node::setRotation(float radians) {
  if (m_rotation == radians)
    return;
  m_rotation = radians;
  markPaintDirty();
}

void Node::setScale(float scale) {
  if (m_scale == scale)
    return;
  m_scale = scale;
  markPaintDirty();
}

void Node::setOpacity(float opacity) {
  if (m_opacity == opacity)
    return;
  m_opacity = opacity;
  markPaintDirty();
}

void Node::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  markLayoutDirty();
}

void Node::setHitTestVisible(bool hitTestVisible) { m_hitTestVisible = hitTestVisible; }
void Node::setClipChildren(bool clipChildren) {
  if (m_clipChildren == clipChildren)
    return;
  m_clipChildren = clipChildren;
  markPaintDirty();
}
void Node::setZIndex(std::int32_t zIndex) {
  if (m_zIndex == zIndex)
    return;
  m_zIndex = zIndex;
  markPaintDirty();
}
void Node::setParticipatesInLayout(bool p) {
  if (m_participatesInLayout == p)
    return;
  m_participatesInLayout = p;
  markLayoutDirty();
}

void Node::setInvalidationCallback(std::function<void(NodeInvalidation)> callback) {
  m_invalidationCallback = std::move(callback);
}

void Node::setAnimationManager(AnimationManager* mgr) {
  m_animationManager = mgr;
  for (auto& child : m_children) {
    child->setAnimationManager(mgr);
  }
}

Node* Node::addChild(std::unique_ptr<Node> child) {
  child->m_parent = this;
  if (m_animationManager != nullptr && child->m_animationManager == nullptr) {
    child->setAnimationManager(m_animationManager);
  }
  Node* raw = child.get();
  m_children.push_back(std::move(child));
  markLayoutDirty();
  return raw;
}

Node* Node::insertChildAt(std::size_t index, std::unique_ptr<Node> child) {
  child->m_parent = this;
  if (m_animationManager != nullptr && child->m_animationManager == nullptr) {
    child->setAnimationManager(m_animationManager);
  }
  Node* raw = child.get();
  if (index >= m_children.size()) {
    m_children.push_back(std::move(child));
  } else {
    m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
  }
  markLayoutDirty();
  return raw;
}

std::unique_ptr<Node> Node::removeChild(Node* child) {
  auto it = std::find_if(m_children.begin(), m_children.end(), [child](const auto& p) { return p.get() == child; });
  if (it == m_children.end())
    return nullptr;
  auto removed = std::move(*it);
  m_children.erase(it);
  removed->m_parent = nullptr;
  markLayoutDirty();
  return removed;
}

void Node::markPaintDirty() {
  if (m_paintDirty)
    return;
  m_paintDirty = true;
  if (m_parent)
    m_parent->propagatePaintDirty();
  else
    notifyInvalidated(NodeInvalidation::Paint);
}

void Node::markLayoutDirty() {
  propagateLayoutDirty();
  propagatePaintDirty();
}

void Node::propagatePaintDirty() {
  if (m_paintDirty)
    return;
  m_paintDirty = true;
  if (m_parent)
    m_parent->propagatePaintDirty();
  else
    notifyInvalidated(NodeInvalidation::Paint);
}

void Node::propagateLayoutDirty() {
  if (m_layoutDirty)
    return;
  m_layoutDirty = true;
  if (m_parent)
    m_parent->propagateLayoutDirty();
  else
    notifyInvalidated(NodeInvalidation::Layout);
}

void Node::notifyInvalidated(NodeInvalidation invalidation) {
  if (m_invalidationCallback)
    m_invalidationCallback(invalidation);
}

void Node::clearDirty() {
  m_paintDirty = false;
  m_layoutDirty = false;
  for (auto& child : m_children) {
    if (child->paintDirty() || child->layoutDirty())
      child->clearDirty();
  }
}

void Node::transformedBounds(
    const Node* node, const Mat3& world, float& outLeft, float& outTop, float& outRight, float& outBottom
) {
  const Vec2 corners[] = {
      world.transformPoint(0.0f, 0.0f),
      world.transformPoint(node->width(), 0.0f),
      world.transformPoint(node->width(), node->height()),
      world.transformPoint(0.0f, node->height()),
  };

  outLeft = corners[0].x;
  outTop = corners[0].y;
  outRight = outLeft;
  outBottom = outTop;

  for (const Vec2 corner : corners) {
    outLeft = std::min(outLeft, corner.x);
    outTop = std::min(outTop, corner.y);
    outRight = std::max(outRight, corner.x);
    outBottom = std::max(outBottom, corner.y);
  }
}

void Node::transformedBounds(const Node* node, float& outLeft, float& outTop, float& outRight, float& outBottom) {
  transformedBounds(node, computeWorldTransform(node), outLeft, outTop, outRight, outBottom);
}
