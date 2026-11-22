#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace greeter {

  enum class GreeterComponent : unsigned { Login, Clock, Branding, Power, Appearance, Keyboard, Count };
  inline constexpr std::array<std::string_view, 6> kGreeterComponentIds{"login", "clock",      "branding",
                                                                        "power", "appearance", "keyboard"};
  enum class LoginPresentation { Auto, Full, Minimal };

  struct ComponentPlacement {
    std::optional<bool> visible;
    std::optional<std::string> anchor;
    std::optional<float> x;
    std::optional<float> y;
    std::optional<float> width;
    std::optional<std::string> output;
    // Stored for scene-transform integration.
    std::optional<double> rotation;
    std::optional<double> scale;
    std::optional<bool> flipX;
    std::optional<bool> flipY;
  };
  struct GreeterLayoutConfig {
    // Unset follows the runtime Auto default, but does not override a synced mode.
    std::optional<LoginPresentation> loginPresentation;
    std::array<ComponentPlacement, 6> components;
  };
  struct LayoutRect {
    float x = 0, y = 0, width = 0, height = 0;
    bool visible = false;
  };
  struct GreeterLayout {
    std::array<LayoutRect, 6> components;
    bool narrow = false;
    const LayoutRect& operator[](GreeterComponent id) const { return components[static_cast<unsigned>(id)]; }
  };

  bool validLayoutAnchor(std::string_view anchor);
  bool rectanglesOverlap(const LayoutRect& a, const LayoutRect& b, float gap = 8);
  LayoutRect placeGreeterPopup(
      const LayoutRect& anchor, float popupWidth, float popupHeight, float width, float height, bool upward,
      bool rightAlign = true
  );
  // Required login always stays on every active view. Optional components yield
  // to it and stack inward or disappear if there is no usable space.
  GreeterLayout resolveGreeterLayout(
      const GreeterLayoutConfig& config, float width, float height, float panelHeight, bool minimal,
      std::string_view connector, std::string_view stableIdentifier, float clockHeight = 202, float keyboardWidth = 148
  );

} // namespace greeter
