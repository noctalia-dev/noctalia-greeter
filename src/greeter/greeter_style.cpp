#include "greeter/greeter_style.h"

#include "ui/style.h"

#include <algorithm>

float GreeterStyle::s_uiScale = 1.0f;
float GreeterStyle::s_cornerRadiusScale = 1.0f;

void GreeterStyle::setUiScale(float scale) noexcept { s_uiScale = std::clamp(scale, kMinUiScale, kMaxUiScale); }

float GreeterStyle::uiScale() noexcept { return s_uiScale; }

float GreeterStyle::scaled(float value) noexcept { return value * s_uiScale; }

void GreeterStyle::setCornerRadiusScale(float scale) noexcept {
  s_cornerRadiusScale = std::clamp(scale, 0.0f, 2.0f);
  Style::setCornerRadiusScale(s_cornerRadiusScale);
}

float GreeterStyle::cornerRadiusScale() noexcept { return s_cornerRadiusScale; }

float GreeterStyle::scaledRadius(float radius, float factor) noexcept {
  return radius * s_uiScale * s_cornerRadiusScale * factor;
}

float GreeterStyle::spaceXs() noexcept { return spaceXsBase * s_uiScale; }
float GreeterStyle::spaceSm() noexcept { return spaceSmBase * s_uiScale; }
float GreeterStyle::spaceMd() noexcept { return spaceMdBase * s_uiScale; }
float GreeterStyle::spaceLg() noexcept { return spaceLgBase * s_uiScale; }
float GreeterStyle::spaceXl() noexcept { return spaceXlBase * s_uiScale; }
float GreeterStyle::space2xl() noexcept { return space2xlBase * s_uiScale; }
float GreeterStyle::space3xl() noexcept { return space3xlBase * s_uiScale; }

float GreeterStyle::controlHeight() noexcept { return controlHeightBase * s_uiScale; }
float GreeterStyle::controlHeightSm() noexcept { return controlHeightSmBase * s_uiScale; }
float GreeterStyle::controlHeightLg() noexcept { return controlHeightLgBase * s_uiScale; }

float GreeterStyle::fontSizeCaption() noexcept { return fontSizeCaptionBase * s_uiScale; }
float GreeterStyle::fontSizeBody() noexcept { return fontSizeBodyBase * s_uiScale; }
float GreeterStyle::fontSizeTitle() noexcept { return fontSizeTitleBase * s_uiScale; }
float GreeterStyle::fontSizeHeading() noexcept { return fontSizeHeadingBase * s_uiScale; }
float GreeterStyle::fontSizeDisplay() noexcept { return fontSizeDisplayBase * s_uiScale; }

float GreeterStyle::borderWidth() noexcept { return borderWidthBase * s_uiScale; }

float GreeterStyle::radiusSm() noexcept { return scaledRadius(radiusSmBase); }
float GreeterStyle::radiusMd() noexcept { return scaledRadius(radiusMdBase); }
float GreeterStyle::radiusLg() noexcept { return scaledRadius(radiusLgBase); }
float GreeterStyle::radiusXl() noexcept { return scaledRadius(radiusXlBase); }

float GreeterStyle::scaledRadiusSm(float factor) noexcept { return scaledRadius(radiusSmBase, factor); }
float GreeterStyle::scaledRadiusMd(float factor) noexcept { return scaledRadius(radiusMdBase, factor); }
float GreeterStyle::scaledRadiusLg(float factor) noexcept { return scaledRadius(radiusLgBase, factor); }
float GreeterStyle::scaledRadiusXl(float factor) noexcept { return scaledRadius(radiusXlBase, factor); }
