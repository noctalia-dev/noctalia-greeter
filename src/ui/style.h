#pragma once

class Style {
public:
  static constexpr float kMinUiScale = 1.0f;
  static constexpr float kMaxUiScale = 2.0f;

  static void setUiScale(float scale) noexcept;
  [[nodiscard]] static float uiScale() noexcept;
  [[nodiscard]] static float scaled(float value) noexcept;

  // Spacing
  static constexpr float spaceXsBase = 4.0f;
  static constexpr float spaceSmBase = 8.0f;
  static constexpr float spaceMdBase = 12.0f;
  static constexpr float spaceLgBase = 16.0f;
  static constexpr float spaceXlBase = 24.0f;
  static constexpr float space2xlBase = 32.0f;
  static constexpr float space3xlBase = 48.0f;

  [[nodiscard]] static float spaceXs() noexcept;
  [[nodiscard]] static float spaceSm() noexcept;
  [[nodiscard]] static float spaceMd() noexcept;
  [[nodiscard]] static float spaceLg() noexcept;
  [[nodiscard]] static float spaceXl() noexcept;
  [[nodiscard]] static float space2xl() noexcept;
  [[nodiscard]] static float space3xl() noexcept;

  // Sizes
  static constexpr float controlHeightBase = 36.0f;
  static constexpr float controlHeightSmBase = 30.0f;
  static constexpr float controlHeightLgBase = 44.0f;

  [[nodiscard]] static float controlHeight() noexcept;
  [[nodiscard]] static float controlHeightSm() noexcept;
  [[nodiscard]] static float controlHeightLg() noexcept;

  // Font sizes
  static constexpr float fontSizeCaptionBase = 12.0f;
  static constexpr float fontSizeBodyBase = 14.0f;
  static constexpr float fontSizeTitleBase = 16.0f;
  static constexpr float fontSizeHeadingBase = 20.0f;
  static constexpr float fontSizeDisplayBase = 28.0f;

  [[nodiscard]] static float fontSizeCaption() noexcept;
  [[nodiscard]] static float fontSizeBody() noexcept;
  [[nodiscard]] static float fontSizeTitle() noexcept;
  [[nodiscard]] static float fontSizeHeading() noexcept;
  [[nodiscard]] static float fontSizeDisplay() noexcept;

  // Border
  static constexpr float borderWidthBase = 1.0f;
  [[nodiscard]] static float borderWidth() noexcept;

  // Radii (1:2:3:4 ratio, matches noctalia Style)
  static constexpr float radiusSmBase = 3.0f;
  static constexpr float radiusMdBase = 6.0f;
  static constexpr float radiusLgBase = 9.0f;
  static constexpr float radiusXlBase = 12.0f;
  static constexpr float radiusFull = 9999.0f;

  static void setCornerRadiusScale(float scale) noexcept;
  [[nodiscard]] static float cornerRadiusScale() noexcept;

  [[nodiscard]] static float scaledRadius(float radius, float factor = 1.0f) noexcept;
  [[nodiscard]] static float radiusSm() noexcept;
  [[nodiscard]] static float radiusMd() noexcept;
  [[nodiscard]] static float radiusLg() noexcept;
  [[nodiscard]] static float radiusXl() noexcept;

  [[nodiscard]] static float scaledRadiusSm(float factor = 1.0f) noexcept;
  [[nodiscard]] static float scaledRadiusMd(float factor = 1.0f) noexcept;
  [[nodiscard]] static float scaledRadiusLg(float factor = 1.0f) noexcept;
  [[nodiscard]] static float scaledRadiusXl(float factor = 1.0f) noexcept;

  // Animation
  static constexpr float animFast = 150.0f;
  static constexpr float animNormal = 250.0f;
  static constexpr float animSlow = 400.0f;

private:
  static float s_uiScale;
  static float s_cornerRadiusScale;
};
