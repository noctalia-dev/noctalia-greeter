#include "greeter/greeter_layout.h"

#include <algorithm>
#include <cmath>

namespace greeter {
  namespace {
    LayoutRect anchored(std::string_view anchor, float w, float h, float width, float height, float margin) {
      float x = (width - w) / 2, y = (height - h) / 2;
      if (anchor.ends_with("left"))
        x = margin;
      if (anchor.ends_with("right"))
        x = width - margin - w;
      if (anchor.starts_with("top"))
        y = margin;
      if (anchor.starts_with("bottom"))
        y = height - margin - h;
      return {x, y, w, h, true};
    }
    void contain(LayoutRect& rect, float width, float height, float margin) {
      rect.width = std::min(rect.width, std::max(1.0F, width - 2 * margin));
      rect.height = std::min(rect.height, std::max(1.0F, height - 2 * margin));
      rect.x = std::round(std::clamp(rect.x, margin, std::max(margin, width - margin - rect.width)));
      rect.y = std::round(std::clamp(rect.y, margin, std::max(margin, height - margin - rect.height)));
    }
  } // namespace
  bool validLayoutAnchor(std::string_view anchor) {
    constexpr std::array anchors{"top-left",     "top-center",  "top-right",     "center-left", "center",
                                 "center-right", "bottom-left", "bottom-center", "bottom-right"};
    return std::ranges::find(anchors, anchor) != anchors.end();
  }
  bool rectanglesOverlap(const LayoutRect& a, const LayoutRect& b, float gap) {
    return a.visible
        && b.visible
        && a.x < b.x + b.width + gap
        && a.x + a.width + gap > b.x
        && a.y < b.y + b.height + gap
        && a.y + a.height + gap > b.y;
  }
  LayoutRect placeGreeterPopup(
      const LayoutRect& anchor, float popupWidth, float popupHeight, float width, float height, bool upward,
      bool rightAlign
  ) {
    constexpr float margin = 12, gap = 10;
    const float w = std::clamp(popupWidth, 1.0F, std::max(1.0F, width - 2 * margin));
    const float h = std::clamp(popupHeight, 1.0F, std::max(1.0F, height - 2 * margin));
    const float above = anchor.y - gap - margin;
    const float below = height - margin - anchor.y - anchor.height - gap;
    if (h > (upward ? above : below) && (upward ? below : above) > (upward ? above : below))
      upward = !upward;
    return {
        std::clamp(rightAlign ? anchor.x + anchor.width - w : anchor.x, margin, std::max(margin, width - w - margin)),
        std::clamp(
            upward ? anchor.y - gap - h : anchor.y + anchor.height + gap, margin, std::max(margin, height - h - margin)
        ),
        w, h, true
    };
  }
  GreeterLayout resolveGreeterLayout(
      const GreeterLayoutConfig& config, float width, float height, float panelHeight, bool minimal,
      std::string_view connector, std::string_view stableIdentifier, float clockHeight
  ) {
    GreeterLayout result;
    width = std::max(1.0F, width);
    height = std::max(1.0F, height);
    result.narrow = width < 1000 || height < 600;
    const float margin = std::min(24.0F, std::min(width, height) * 0.04F);
    const float edge = result.narrow ? margin : std::clamp(width * 0.06F, 64.0F, 128.0F);
    const float chromeMargin = result.narrow ? margin : 48;
    const auto& login = config.components[0];
    const float panelW = std::clamp(login.width.value_or(minimal ? 360.0F : 420.0F), 280.0F, 640.0F);
    result.components[0] = anchored(
        login.anchor.value_or(result.narrow ? "center" : "center-right"), panelW, panelHeight, width, height, edge
    );
    for (unsigned i = 1; i < 6; i++) {
      const auto& settings = config.components[i];
      float w = 0, h = 0;
      std::string_view anchor = "bottom-right";
      switch (static_cast<GreeterComponent>(i)) {
      case GreeterComponent::Clock:
        w = std::min(400.0F, width - 2 * edge);
        h = clockHeight;
        anchor = result.narrow ? "top-center" : "center-left";
        break;
      case GreeterComponent::Branding:
        w = 140;
        h = 30;
        anchor = "top-left";
        break;
      case GreeterComponent::Power:
        w = 88;
        h = 36;
        break;
      case GreeterComponent::Appearance:
        w = 140;
        h = 36;
        break;
      case GreeterComponent::Keyboard:
        w = 148;
        h = 36;
        break;
      default:
        break;
      }
      auto& rect = result.components[i];
      rect = anchored(
          settings.anchor.value_or(std::string(anchor)), settings.width.value_or(w), h, width, height,
          i == 1 ? edge : chromeMargin
      );
      if (!settings.anchor && i == 4)
        rect.x -= 98;
      if (!settings.anchor && i == 5)
        rect.x -= 248;
      if (!settings.anchor && i == 1 && result.narrow)
        rect.y = chromeMargin + 48;
      rect.visible = settings.visible.value_or(true)
          && (!settings.output || *settings.output == connector || *settings.output == stableIdentifier);
    }
    if (config.components[1].anchor == "above-panel") {
      auto& clock = result.components[1];
      const auto& panel = result.components[0];
      clock.x = panel.x + (panel.width - clock.width) / 2;
      clock.y = panel.y - clock.height - 24;
      clock.visible = clock.visible && clock.y >= margin;
    }
    for (unsigned i = 0; i < 6; i++) {
      auto& rect = result.components[i];
      const auto& settings = config.components[i];
      rect.x += settings.x.value_or(0);
      rect.y += settings.y.value_or(0);
      contain(rect, width, height, margin);
    }
    // Login is mandatory and cannot be hidden/pinned away through component settings.
    result.components[0].visible = true;
    for (unsigned i = 1; i < 6; i++) {
      auto& rect = result.components[i];
      if (!rect.visible)
        continue;
      auto collides = [&] {
        for (unsigned previous = 0; previous < i; previous++)
          if (rectanglesOverlap(rect, result.components[previous]))
            return true;
        return false;
      };
      const bool bottom = rect.y > height / 2;
      for (unsigned tries = 0; tries < 6 && collides(); tries++) {
        rect.y += (bottom ? -1 : 1) * (rect.height + 10);
        if (rect.y < margin || rect.y + rect.height > height - margin) {
          rect.visible = false;
          break;
        }
      }
      if (collides())
        rect.visible = false;
    }
    return result;
  }
} // namespace greeter
