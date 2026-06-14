#pragma once

#include "render/core/mat3.h"
#include "render/core/renderer.h"

#include <cstdint>

class AnimationManager;
#include <functional>
#include <memory>
#include <vector>

enum class NodeInvalidation { Paint, Layout };

struct LayoutSize {
  float width = 0.0f;
  float height = 0.0f;
};

struct LayoutRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

class LayoutConstraints {
public:
  static LayoutConstraints unconstrained() noexcept;
  static LayoutConstraints available(float width, float height) noexcept;
  static LayoutConstraints exact(float width, float height) noexcept;

  void setMaxWidth(float width) noexcept;
  void setMaxHeight(float height) noexcept;
  void setExactWidth(float width) noexcept;
  void setExactHeight(float height) noexcept;

  [[nodiscard]] bool hasExactWidth() const noexcept;
  [[nodiscard]] bool hasExactHeight() const noexcept;
  [[nodiscard]] float constrainWidth(float width) const noexcept;
  [[nodiscard]] float constrainHeight(float height) const noexcept;
  [[nodiscard]] LayoutSize constrain(LayoutSize size) const noexcept;

  float minWidth = 0.0f;
  float minHeight = 0.0f;
  float maxWidth = 0.0f;
  float maxHeight = 0.0f;
  bool hasMaxWidth = false;
  bool hasMaxHeight = false;
};

enum class NodeType : std::uint8_t {
  Base,
  Rect,
  Text,
  Image,
  Glyph,
  Wallpaper,
  InputArea,
  Container,
};

class Node {
public:
  explicit Node(NodeType type = NodeType::Base);
  virtual ~Node();

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  // Layout
  void layout(Renderer& renderer);
  [[nodiscard]] LayoutSize
  measure(Renderer& renderer, const LayoutConstraints& constraints = LayoutConstraints::unconstrained());
  void arrange(Renderer& renderer, const LayoutRect& rect);

  // Hit testing
  [[nodiscard]] bool containsScenePoint(float sceneX, float sceneY) const;
  [[nodiscard]] static Node* hitTest(Node* root, float x, float y);
  static void transformedBounds(const Node* node, float& outLeft, float& outTop, float& outRight, float& outBottom);
  static void transformedBounds(
      const Node* node, const Mat3& world, float& outLeft, float& outTop, float& outRight, float& outBottom
  );

  // Properties
  [[nodiscard]] float x() const noexcept { return m_x; }
  [[nodiscard]] float y() const noexcept { return m_y; }
  [[nodiscard]] float width() const noexcept { return m_width; }
  [[nodiscard]] float height() const noexcept { return m_height; }
  [[nodiscard]] float rotation() const noexcept { return m_rotation; }
  [[nodiscard]] float scale() const noexcept { return m_scale; }
  [[nodiscard]] float opacity() const noexcept { return m_opacity; }
  [[nodiscard]] bool visible() const noexcept { return m_visible; }
  [[nodiscard]] bool hitTestVisible() const noexcept { return m_hitTestVisible; }
  [[nodiscard]] bool clipChildren() const noexcept { return m_clipChildren; }
  [[nodiscard]] std::int32_t zIndex() const noexcept { return m_zIndex; }
  [[nodiscard]] NodeType type() const noexcept { return m_type; }
  [[nodiscard]] bool paintDirty() const noexcept { return m_paintDirty; }
  [[nodiscard]] bool layoutDirty() const noexcept { return m_layoutDirty; }
  [[nodiscard]] bool arrangingByLayout() const noexcept { return m_arranging; }
  [[nodiscard]] bool sizeAssignedByLayout() const noexcept { return m_sizeAssignedByLayout; }
  [[nodiscard]] const std::vector<std::unique_ptr<Node>>& children() const noexcept { return m_children; }
  [[nodiscard]] Node* parent() const noexcept { return m_parent; }

  void setPosition(float x, float y);
  void setSize(float width, float height);
  void setFrameSize(float width, float height);
  void setRotation(float radians);
  void setScale(float scale);
  void setOpacity(float opacity);
  void setVisible(bool visible);
  void setHitTestVisible(bool hitTestVisible);
  void setClipChildren(bool clipChildren);
  void setZIndex(std::int32_t zIndex);
  void setParticipatesInLayout(bool participatesInLayout);
  [[nodiscard]] bool participatesInLayout() const noexcept { return m_participatesInLayout; }

  // Children
  Node* addChild(std::unique_ptr<Node> child);
  Node* insertChildAt(std::size_t index, std::unique_ptr<Node> child);
  std::unique_ptr<Node> removeChild(Node* child);

  void setAnimationManager(AnimationManager* mgr);
  [[nodiscard]] AnimationManager* animationManager() const noexcept { return m_animationManager; }

  // Dirty tracking
  void markPaintDirty();
  void markLayoutDirty();
  void clearDirty();

  void setInvalidationCallback(std::function<void(NodeInvalidation)> callback);

protected:
  virtual void doLayout(Renderer& renderer);
  virtual LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints);
  virtual void doArrange(Renderer& renderer, const LayoutRect& rect);

  LayoutSize measureByLayout(Renderer& renderer, const LayoutConstraints& constraints);
  void arrangeByLayout(Renderer& renderer, const LayoutRect& rect);

  Node* m_parent = nullptr;
  std::vector<std::unique_ptr<Node>> m_children;

private:
  static Node* hitTestImpl(Node* node, float px, float py);
  void propagatePaintDirty();
  void propagateLayoutDirty();
  void notifyInvalidated(NodeInvalidation invalidation);

  NodeType m_type;
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_width = 0.0f;
  float m_height = 0.0f;
  float m_rotation = 0.0f;
  float m_scale = 1.0f;
  float m_opacity = 1.0f;
  bool m_visible = true;
  bool m_hitTestVisible = true;
  bool m_clipChildren = false;
  bool m_participatesInLayout = true;
  bool m_arranging = false;
  bool m_sizeAssignedByLayout = false;
  bool m_paintDirty = false;
  bool m_layoutDirty = false;
  std::int32_t m_zIndex = 0;
  AnimationManager* m_animationManager = nullptr;
  std::function<void(NodeInvalidation)> m_invalidationCallback;
};
