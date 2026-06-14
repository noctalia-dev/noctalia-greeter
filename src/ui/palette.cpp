#include "ui/palette.h"

#include "theme/builtin_palettes.h"

#include <algorithm>

Palette palette = noctalia::theme::findBuiltinPalette("Noctalia")->dark.palette;

namespace {
  Signal<>& paletteSignal() {
    static Signal<> s_signal;
    return s_signal;
  }
} // namespace

const Color& colorForRole(ColorRole role) noexcept {
  switch (role) {
  case ColorRole::Primary:
    return palette.primary;
  case ColorRole::OnPrimary:
    return palette.onPrimary;
  case ColorRole::Secondary:
    return palette.secondary;
  case ColorRole::OnSecondary:
    return palette.onSecondary;
  case ColorRole::Tertiary:
    return palette.tertiary;
  case ColorRole::OnTertiary:
    return palette.onTertiary;
  case ColorRole::Error:
    return palette.error;
  case ColorRole::OnError:
    return palette.onError;
  case ColorRole::Surface:
    return palette.surface;
  case ColorRole::OnSurface:
    return palette.onSurface;
  case ColorRole::SurfaceVariant:
    return palette.surfaceVariant;
  case ColorRole::OnSurfaceVariant:
    return palette.onSurfaceVariant;
  case ColorRole::Outline:
    return palette.outline;
  case ColorRole::Shadow:
    return palette.shadow;
  case ColorRole::Hover:
    return palette.hover;
  case ColorRole::OnHover:
    return palette.onHover;
  }
  return palette.surface;
}

Color colorForRole(ColorRole role, float alpha) noexcept { return colorWithAlpha(colorForRole(role), alpha); }

std::optional<ColorRole> colorRoleFromToken(std::string_view token) {
  for (const auto& t : kColorRoleTokens) {
    if (t.token == token)
      return t.role;
  }
  return std::nullopt;
}

std::string_view colorRoleToken(ColorRole role) noexcept {
  for (const auto& t : kColorRoleTokens) {
    if (t.role == role)
      return t.token;
  }
  return "";
}

ColorSpec colorSpecFromRole(ColorRole role, float alpha) noexcept {
  return ColorSpec{.role = role, .fixed = clearColor(), .alpha = alpha};
}

ColorSpec fixedColorSpec(const Color& color) noexcept {
  return ColorSpec{.role = std::nullopt, .fixed = color, .alpha = 1.0f};
}

Color resolveColorSpec(const ColorSpec& color) noexcept {
  Color resolved = color.role.has_value() ? colorForRole(*color.role) : color.fixed;
  resolved.a *= color.alpha;
  return resolved;
}

Signal<>& paletteChanged() { return paletteSignal(); }

void setPalette(const Palette& p) {
  if (palette == p) {
    return;
  }
  palette = p;
  paletteChanged().emit();
}

Palette lerpPalette(const Palette& a, const Palette& b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return {
      lerpColor(a.primary, b.primary, t),
      lerpColor(a.onPrimary, b.onPrimary, t),
      lerpColor(a.secondary, b.secondary, t),
      lerpColor(a.onSecondary, b.onSecondary, t),
      lerpColor(a.tertiary, b.tertiary, t),
      lerpColor(a.onTertiary, b.onTertiary, t),
      lerpColor(a.error, b.error, t),
      lerpColor(a.onError, b.onError, t),
      lerpColor(a.surface, b.surface, t),
      lerpColor(a.onSurface, b.onSurface, t),
      lerpColor(a.surfaceVariant, b.surfaceVariant, t),
      lerpColor(a.onSurfaceVariant, b.onSurfaceVariant, t),
      lerpColor(a.outline, b.outline, t),
      lerpColor(a.shadow, b.shadow, t),
      lerpColor(a.hover, b.hover, t),
      lerpColor(a.onHover, b.onHover, t),
  };
}
