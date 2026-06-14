#include "render/core/color.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

Color lerpColor(const Color& a, const Color& b, float t) noexcept {
  t = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
      a.a + (b.a - a.a) * t,
  };
}

bool tryParseHexColor(std::string_view hex, Color& out) noexcept {
  if (hex.empty())
    return false;

  if (hex[0] == '#') {
    hex = hex.substr(1);
  }

  std::uint32_t value = 0;

  switch (hex.size()) {
  case 3: {
    std::uint32_t r = 0, g = 0, b = 0;
    if (sscanf(hex.data(), "%1x%1x%1x", &r, &g, &b) != 3)
      return false;
    out.r = static_cast<float>(r) / 15.0f;
    out.g = static_cast<float>(g) / 15.0f;
    out.b = static_cast<float>(b) / 15.0f;
    out.a = 1.0f;
    return true;
  }
  case 4: {
    std::uint32_t r = 0, g = 0, b = 0, a = 0;
    if (sscanf(hex.data(), "%1x%1x%1x%1x", &r, &g, &b, &a) != 4)
      return false;
    out.r = static_cast<float>(r) / 15.0f;
    out.g = static_cast<float>(g) / 15.0f;
    out.b = static_cast<float>(b) / 15.0f;
    out.a = static_cast<float>(a) / 15.0f;
    return true;
  }
  case 6: {
    if (sscanf(hex.data(), "%6x", &value) != 1)
      return false;
    out.r = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
    out.g = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
    out.b = static_cast<float>(value & 0xFF) / 255.0f;
    out.a = 1.0f;
    return true;
  }
  case 8: {
    if (sscanf(hex.data(), "%8x", &value) != 1)
      return false;
    out.r = static_cast<float>((value >> 24) & 0xFF) / 255.0f;
    out.g = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
    out.b = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
    out.a = static_cast<float>(value & 0xFF) / 255.0f;
    return true;
  }
  default:
    return false;
  }
}

Color colorWithAlpha(const Color& color, float alpha) noexcept {
  return {color.r, color.g, color.b, std::clamp(alpha, 0.0f, 1.0f)};
}
