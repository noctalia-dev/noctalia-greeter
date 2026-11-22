#pragma once

#include "greeter/greeter_layout.h"

#include <string>
#include <string_view>

class Node;
class Box;
class Button;
class Input;
class Label;
class Glyph;
class ImageNode;
class RectNode;
class Renderer;

namespace greeter {
  // Transform top-level component nodes around their shared visual center.
  void applyComponentTransform(Node& node, const LayoutRect& bounds, const ComponentTransform& transform);
} // namespace greeter

struct LoginPanelNodes {
  Box* background;
  Input* input;
  Button* submit;
  Label* feedback;
  Label* name;
};
struct LoginPanelView {
  bool minimal = false;
  bool passwordStep = true;
  bool identitySearch = false;
  bool blocked = false;
  bool error = false;
  bool busy = false;
  std::string name;
  std::string username;
  std::string prompt;
  std::string feedback;
};
struct LoginPanelGeometry {
  greeter::LayoutRect identity;
  greeter::LayoutRect session;
  float avatarX = 0, avatarY = 0, avatarSize = 0;
};

// The scene owns the nodes; these small components own their presentation and
// layout. Authentication, menus and input dispatch remain with GreeterSurface.
class GreeterLoginPanel {
public:
  GreeterLoginPanel(Node& scene, LoginPanelNodes nodes);
  void applyTransform(const greeter::LayoutRect& bounds, const greeter::ComponentTransform& transform);
  float measure(Renderer& renderer, float width, float availableHeight, const LoginPanelView& view);
  LoginPanelGeometry layout(Renderer& renderer, const greeter::LayoutRect& rect, const LoginPanelView& view);

private:
  LoginPanelNodes m_nodes;
  Label* m_title;
  Label* m_subtitle;
  Label* m_prompt;
  Glyph* m_hostGlyph;
  std::string m_hostname;
  RectNode* m_avatar;
  float m_padding = 32, m_inputY = 225, m_inputH = 48, m_promptY = 200, m_identityY = 112, m_identityH = 64;
  float m_sessionY = 332, m_buttonY = 388, m_feedbackY = 285, m_buttonH = 48;
  bool m_dense = false;
};

class GreeterChrome {
public:
  explicit GreeterChrome(Node& scene);
  void applyKeyboardTransform(const greeter::LayoutRect& bounds, const greeter::ComponentTransform& transform);
  void layout(
      Renderer& renderer, const greeter::GreeterLayout& layout, ImageNode& logo, bool hasLogo,
      std::string_view keyboardName, float width, float height, bool wallpaper
  );

private:
  Label* m_keyboard;
  Glyph* m_keyboardGlyph;
  RectNode* m_orbit1;
  RectNode* m_orbit2;
};
