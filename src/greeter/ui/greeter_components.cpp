#include "greeter/ui/greeter_components.h"

#include "render/core/renderer.h"
#include "render/scene/image_node.h"
#include "render/scene/node.h"
#include "render/scene/rect_node.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unistd.h>

namespace {
  template <class T> T* create(Node& scene, int z = 6) {
    auto node = std::make_unique<T>();
    auto* ptr = node.get();
    ptr->setZIndex(z);
    scene.addChild(std::move(node));
    return ptr;
  }
  void label(
      Label* node, Renderer& renderer, std::string_view value, float x, float y, float size, Color color,
      float width = 0, FontWeight weight = FontWeight::Normal, int lines = 1
  ) {
    node->setText(value);
    node->setFontSize(size);
    node->setColor(color);
    node->setFontWeight(weight);
    node->setMaxWidth(width);
    node->setMaxLines(lines);
    node->measure(renderer);
    node->setPosition(x, y);
    node->setVisible(true);
  }
  void rect(
      RectNode* node, float x, float y, float w, float h, Color fill, float radius, Color border = {},
      float borderWidth = 0
  ) {
    node->setVisible(true);
    node->setPosition(x, y);
    node->setSize(w, h);
    node->setStyle(
        {.fill = fill,
         .border = border,
         .radius = radius * Style::cornerRadiusScale(),
         .borderWidth = borderWidth,
         .shadowExclusionRadius = {}}
    );
  }
} // namespace
void greeter::applyComponentTransform(Node& node, const LayoutRect& bounds, const ComponentTransform& transform) {
  const float cx = bounds.x + bounds.width / 2, cy = bounds.y + bounds.height / 2;
  const float nx = node.x() + node.width() / 2, ny = node.y() + node.height() / 2;
  const float sx = transform.scale * (transform.flipX ? -1.0F : 1.0F);
  const float sy = transform.scale * (transform.flipY ? -1.0F : 1.0F);
  const float dx = (nx - cx) * sx, dy = (ny - cy) * sy;
  const float cosine = std::cos(transform.rotation), sine = std::sin(transform.rotation);
  node.setRotation(transform.rotation);
  node.setScale(sx, sy);
  node.setTranslate(cx + cosine * dx - sine * dy - nx, cy + sine * dx + cosine * dy - ny);
}

void GreeterLoginPanel::applyTransform(
    const greeter::LayoutRect& bounds, const greeter::ComponentTransform& transform
) {
  for (Node* node : std::array<Node*, 10>{
           m_nodes.background, m_nodes.input, m_nodes.submit, m_nodes.feedback, m_nodes.name, m_title, m_subtitle,
           m_prompt, m_hostGlyph, m_avatar
       })
    greeter::applyComponentTransform(*node, bounds, transform);
}

void GreeterChrome::applyKeyboardTransform(
    const greeter::LayoutRect& bounds, const greeter::ComponentTransform& transform
) {
  greeter::applyComponentTransform(*m_keyboard, bounds, transform);
  greeter::applyComponentTransform(*m_keyboardGlyph, bounds, transform);
}

GreeterLoginPanel::GreeterLoginPanel(Node& scene, LoginPanelNodes nodes) : m_nodes(nodes) {
  m_title = create<Label>(scene);
  m_subtitle = create<Label>(scene);
  m_prompt = create<Label>(scene);
  m_hostGlyph = create<Glyph>(scene);
  m_hostGlyph->setHitTestVisible(false);
  std::array<char, 256> hostname{};
  if (gethostname(hostname.data(), hostname.size() - 1) == 0)
    m_hostname = hostname.data();
  m_avatar = create<RectNode>(scene, 6);
  m_avatar->setHitTestVisible(false);
  m_nodes.background->setZIndex(4);
}
float GreeterLoginPanel::measure(Renderer& renderer, float width, float availableHeight, const LoginPanelView& view) {
  m_dense = availableHeight < 600;
  m_padding = view.minimal || m_dense ? 24 : 32;
  const float contentW = std::max(1.0F, width - 2 * m_padding);
  label(
      m_prompt, renderer, view.prompt, 0, 0, 13, colorForRole(ColorRole::OnSurface), contentW, FontWeight::Medium,
      m_dense ? 2 : 3
  );
  const float extra = std::max(0.0F, m_prompt->height() - 18);
  label(
      m_nodes.feedback, renderer, view.feedback, 0, 0, 12,
      colorForRole(view.error ? ColorRole::Error : ColorRole::OnSurfaceVariant), contentW, FontWeight::Normal,
      m_dense ? 2 : 4
  );
  const float feedbackH = view.feedback.empty() ? 0 : m_nodes.feedback->height();
  m_inputH = m_dense ? 40 : (view.minimal ? 44 : 48);
  m_buttonH = m_dense ? 40 : 48;
  if (view.minimal) {
    m_identityY = 24;
    m_identityH = 48;
    m_promptY = m_dense ? 82 : 102;
    m_inputY = m_promptY + 24 + extra;
    m_feedbackY = m_inputY + m_inputH + 12;
    m_buttonY = m_inputY + m_inputH + (feedbackH ? 12 + feedbackH + 32 : 28);
    m_sessionY = 0;
  } else {
    m_identityY = m_dense ? 64 : 112;
    m_identityH = m_dense ? 48 : 64;
    m_promptY = m_identityY + m_identityH + 24;
    m_inputY = m_promptY + 25 + extra;
    m_feedbackY = m_inputY + m_inputH + 12;
    // Account search replaces the identity row without collapsing the form.
    // Keep the card and its controls anchored while switching accounts.
    m_sessionY = std::max(m_dense ? 244.0F : 332.0F, m_feedbackY + feedbackH + 16);
    m_buttonY = m_sessionY + (m_dense ? 46 : 56);
  }
  return m_buttonY + m_buttonH + m_padding;
}
LoginPanelGeometry
GreeterLoginPanel::layout(Renderer& renderer, const greeter::LayoutRect& bounds, const LoginPanelView& view) {
  const float x = bounds.x + m_padding, y = bounds.y, cw = bounds.width - 2 * m_padding;
  m_nodes.background->setVisible(true);
  m_nodes.background->setPosition(bounds.x, y);
  m_nodes.background->setSize(bounds.width, bounds.height);
  m_nodes.background->setStyle(
      {.fill = colorForRole(ColorRole::SurfaceContainer),
       .border = colorForRole(ColorRole::OutlineVariant),
       .radius = 24 * Style::cornerRadiusScale(),
       .borderWidth = 1,
       .shadowExclusionRadius = {}}
  );
  if (view.minimal) {
    m_title->setVisible(false);
    m_nodes.name->setVisible(true);
    label(
        m_nodes.name, renderer, view.name, x + 54, y + 25, 18, colorForRole(ColorRole::OnSurface), cw - 54,
        FontWeight::Medium
    );
    const bool hasHostname = !m_hostname.empty();
    m_hostGlyph->setVisible(hasHostname);
    if (hasHostname) {
      m_hostGlyph->setGlyph("device-desktop");
      m_hostGlyph->setGlyphSize(12);
      m_hostGlyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
      m_hostGlyph->measure(renderer);
      m_hostGlyph->setPosition(x + 54, y + 56);
    }
    const float subtitleX = x + 54 + (hasHostname ? 18 : 0);
    label(
        m_subtitle, renderer, hasHostname ? m_hostname : "Welcome back", subtitleX, y + 54, 12,
        colorForRole(ColorRole::OnSurfaceVariant), x + cw - subtitleX
    );
  } else {
    m_hostGlyph->setVisible(false);
    label(
        m_title, renderer, "Welcome back", x, y + (m_dense ? 20 : 31), m_dense ? 24 : 28,
        colorForRole(ColorRole::OnSurface), cw, FontWeight::Medium
    );
    label(
        m_subtitle, renderer, m_hostname.empty() ? "Sign in to your desktop" : "Sign in to " + m_hostname, x,
        y + (m_dense ? 46 : 76), m_dense ? 11 : 14, colorForRole(ColorRole::OnSurfaceVariant), cw
    );
    m_nodes.name->setVisible(false);
  }
  m_prompt->setVisible(view.passwordStep || !view.minimal);
  m_prompt->setPosition(x, y + m_promptY);
  m_nodes.input->setVisible(view.passwordStep || !view.minimal);
  m_nodes.input->setControlHeight(m_inputH);
  m_nodes.input->setFontSize(15);
  m_nodes.input->setFrameRadius(12 * Style::cornerRadiusScale());
  m_nodes.input->setPosition(x, y + m_inputY);
  m_nodes.input->setSize(cw, m_inputH);
  m_nodes.input->layout(renderer);
  m_nodes.input->setInvalid(view.error);
  m_nodes.feedback->setVisible(!view.feedback.empty());
  m_nodes.feedback->setPosition(x, y + m_feedbackY);
  m_nodes.submit->setVisible(true);
  m_nodes.submit->setText(view.passwordStep ? (view.busy ? "Signing in…" : "Sign in") : "Continue");
  m_nodes.submit->setVariant(ButtonVariant::Primary);
  m_nodes.submit->setFontSize(14);
  m_nodes.submit->setControlHeight(m_buttonH);
  m_nodes.submit->setRadius(12 * Style::cornerRadiusScale());
  m_nodes.submit->setPosition(x, y + m_buttonY);
  m_nodes.submit->setSize(cw, m_buttonH);
  m_nodes.submit->layout(renderer);
  const float avatarSize = view.minimal ? 40 : (m_dense ? 32 : 38);
  const float avatarX = x + (view.minimal ? 0 : 13), avatarY = y + m_identityY + (m_identityH - avatarSize) / 2;
  rect(m_avatar, avatarX, avatarY, avatarSize, avatarSize, colorForRole(ColorRole::Secondary, 0.14F), avatarSize / 2);
  m_avatar->setVisible(!view.identitySearch);
  return {
      {x, y + m_identityY, cw, m_identityH, !view.minimal},
      {x, y + m_sessionY, cw, m_dense ? 36.0F : 40.0F, !view.minimal},
      avatarX,
      avatarY,
      avatarSize
  };
}

GreeterChrome::GreeterChrome(Node& scene) {
  m_keyboard = create<Label>(scene);
  m_keyboardGlyph = create<Glyph>(scene);
  m_keyboardGlyph->setHitTestVisible(false);
  m_orbit1 = create<RectNode>(scene, 2);
  m_orbit2 = create<RectNode>(scene, 2);
  m_orbit1->setHitTestVisible(false);
  m_orbit2->setHitTestVisible(false);
}
void GreeterChrome::layout(
    Renderer& renderer, const greeter::GreeterLayout& layout, ImageNode& logo, bool hasLogo,
    std::string_view keyboardName, float width, float height, bool wallpaper
) {
  using Id = greeter::GreeterComponent;
  const auto placeGlyph = [&](Glyph* glyph, std::string_view name, float x, float y, float size) {
    glyph->setVisible(true);
    glyph->setGlyph(name);
    glyph->setGlyphSize(size);
    glyph->setColor(colorForRole(ColorRole::OnSurfaceVariant));
    glyph->measure(renderer);
    glyph->setPosition(x, y);
  };
  for (auto* orbit : {m_orbit1, m_orbit2})
    orbit->setVisible(!wallpaper && !layout.narrow);
  if (!wallpaper && !layout.narrow) {
    rect(
        m_orbit1, -width * .28F, height * .13F, width * .83F, width * .83F, clearColor(), width,
        colorForRole(ColorRole::Secondary, .045F), 1
    );
    rect(
        m_orbit2, -width * .27F, height * .16F, width * .78F, width * .78F, clearColor(), width,
        colorForRole(ColorRole::Secondary, .035F), 1
    );
  }
  const auto& brand = layout[Id::Branding];
  logo.setVisible(brand.visible && hasLogo);
  if (brand.visible && hasLogo) {
    logo.setPosition(brand.x, brand.y);
    logo.setSize(28, 28);
  }
  const auto& keyboard = layout[Id::Keyboard];
  m_keyboard->setVisible(keyboard.visible && !keyboardName.empty());
  m_keyboardGlyph->setVisible(m_keyboard->visible());
  if (m_keyboard->visible()) {
    label(
        m_keyboard, renderer, keyboardName, keyboard.x + 36, keyboard.y, 13, colorForRole(ColorRole::OnSurfaceVariant),
        std::max(1.0F, keyboard.width - 48)
    );
    m_keyboard->setBaselineMode(LabelBaselineMode::InkCentered);
    m_keyboard->measure(renderer);
    placeGlyph(m_keyboardGlyph, "keyboard", keyboard.x, keyboard.y, 16);
    const float groupWidth = m_keyboardGlyph->width() + 8 + m_keyboard->width();
    const float left = keyboard.x + (keyboard.width - groupWidth) / 2;
    m_keyboard->setPosition(
        left + m_keyboardGlyph->width() + 8, keyboard.y + (keyboard.height - m_keyboard->height()) / 2
    );
    m_keyboardGlyph->setPosition(left, keyboard.y + (keyboard.height - m_keyboardGlyph->height()) / 2);
  }
}
