#include "wayland/output_span_layout.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

namespace {

  [[nodiscard]] bool
  expectSpan(const std::string_view name, const WallpaperSpanParams& actual, const WallpaperSpanParams& expected) {
    if (actual == expected) {
      return true;
    }
    std::cerr
        << name
        << ": expected {"
        << expected.offsetX
        << ", "
        << expected.offsetY
        << ", "
        << expected.monitorWidth
        << ", "
        << expected.monitorHeight
        << ", "
        << expected.totalWidth
        << ", "
        << expected.totalHeight
        << "}, got {"
        << actual.offsetX
        << ", "
        << actual.offsetY
        << ", "
        << actual.monitorWidth
        << ", "
        << actual.monitorHeight
        << ", "
        << actual.totalWidth
        << ", "
        << actual.totalHeight
        << "}\n";
    return false;
  }

  [[nodiscard]] bool expectOrientedSize(
      const std::int32_t transform, const std::int32_t expectedWidth, const std::int32_t expectedHeight
  ) {
    const auto [actualWidth, actualHeight] = greeter::orientedOutputPixelSize(1920, 1080, transform);
    if (actualWidth == expectedWidth && actualHeight == expectedHeight) {
      return true;
    }
    std::cerr
        << "transform "
        << transform
        << ": expected "
        << expectedWidth
        << 'x'
        << expectedHeight
        << ", got "
        << actualWidth
        << 'x'
        << actualHeight
        << '\n';
    return false;
  }

} // namespace

int main() {
  bool passed = true;
  constexpr WallpaperSpanParams unavailable{};

  for (const std::int32_t transform : std::array<std::int32_t, 4>{0, 2, 4, 6}) {
    passed = expectOrientedSize(transform, 1920, 1080) && passed;
  }
  for (const std::int32_t transform : std::array<std::int32_t, 4>{1, 3, 5, 7}) {
    passed = expectOrientedSize(transform, 1080, 1920) && passed;
  }

  {
    constexpr std::array outputs{
        greeter::OutputSpanGeometry{.x = 0, .y = 0, .width = 1920, .height = 1080},
        greeter::OutputSpanGeometry{.x = 1920, .y = 0, .width = 2560, .height = 1440},
    };
    passed = expectSpan(
                 "side-by-side left", greeter::computeWallpaperSpanParams(outputs, 0),
                 WallpaperSpanParams{
                     .offsetX = 0.0f,
                     .offsetY = 0.0f,
                     .monitorWidth = 1920.0f,
                     .monitorHeight = 1080.0f,
                     .totalWidth = 4480.0f,
                     .totalHeight = 1440.0f,
                 }
             )
        && passed;
    passed = expectSpan(
                 "side-by-side right", greeter::computeWallpaperSpanParams(outputs, 1),
                 WallpaperSpanParams{
                     .offsetX = 1920.0f,
                     .offsetY = 0.0f,
                     .monitorWidth = 2560.0f,
                     .monitorHeight = 1440.0f,
                     .totalWidth = 4480.0f,
                     .totalHeight = 1440.0f,
                 }
             )
        && passed;
  }

  {
    constexpr std::array outputs{
        greeter::OutputSpanGeometry{.x = -1200, .y = 240, .width = 1200, .height = 1920},
        greeter::OutputSpanGeometry{.x = 0, .y = -120, .width = 2560, .height = 1440},
        greeter::OutputSpanGeometry{.x = 640, .y = 1320, .width = 1920, .height = 1080},
    };
    passed = expectSpan(
                 "negative staggered left", greeter::computeWallpaperSpanParams(outputs, 0),
                 WallpaperSpanParams{
                     .offsetX = 0.0f,
                     .offsetY = 360.0f,
                     .monitorWidth = 1200.0f,
                     .monitorHeight = 1920.0f,
                     .totalWidth = 3760.0f,
                     .totalHeight = 2520.0f,
                 }
             )
        && passed;
    passed = expectSpan(
                 "negative staggered center", greeter::computeWallpaperSpanParams(outputs, 1),
                 WallpaperSpanParams{
                     .offsetX = 1200.0f,
                     .offsetY = 0.0f,
                     .monitorWidth = 2560.0f,
                     .monitorHeight = 1440.0f,
                     .totalWidth = 3760.0f,
                     .totalHeight = 2520.0f,
                 }
             )
        && passed;
    passed = expectSpan(
                 "negative staggered bottom", greeter::computeWallpaperSpanParams(outputs, 2),
                 WallpaperSpanParams{
                     .offsetX = 1840.0f,
                     .offsetY = 1440.0f,
                     .monitorWidth = 1920.0f,
                     .monitorHeight = 1080.0f,
                     .totalWidth = 3760.0f,
                     .totalHeight = 2520.0f,
                 }
             )
        && passed;
  }

  {
    constexpr std::array single{
        greeter::OutputSpanGeometry{.x = -320, .y = 180, .width = 3840, .height = 2160},
    };
    passed = expectSpan("single output falls back to crop", greeter::computeWallpaperSpanParams(single, 0), unavailable)
        && passed;
    passed = expectSpan("invalid target index", greeter::computeWallpaperSpanParams(single, 1), unavailable) && passed;
  }

  {
    constexpr std::array invalid{
        greeter::OutputSpanGeometry{.x = 0, .y = 0, .width = 1920, .height = 1080},
        greeter::OutputSpanGeometry{.x = 1920, .y = 0, .width = 0, .height = 1080},
    };
    passed =
        expectSpan("invalid output geometry", greeter::computeWallpaperSpanParams(invalid, 0), unavailable) && passed;
  }

  passed = expectSpan(
               "empty output geometry",
               greeter::computeWallpaperSpanParams(std::span<const greeter::OutputSpanGeometry>{}, 0), unavailable
           )
      && passed;

  return passed ? 0 : 1;
}
