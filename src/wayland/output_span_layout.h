#pragma once

#include "render/core/wallpaper_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace greeter {
  // wl_output_transform assigns odd values to the four quarter-turn variants
  // (90, 270, flipped-90, and flipped-270). Keep the explicit values here so
  // this geometry helper remains independent of Wayland client headers.
  [[nodiscard]] constexpr bool outputTransformSwapsDimensions(const std::int32_t transform) noexcept {
    return transform == 1 || transform == 3 || transform == 5 || transform == 7;
  }

  [[nodiscard]] constexpr std::pair<std::int32_t, std::int32_t>
  orientedOutputPixelSize(const std::int32_t width, const std::int32_t height, const std::int32_t transform) noexcept {
    return outputTransformSwapsDimensions(transform) ? std::pair{height, width} : std::pair{width, height};
  }

  struct OutputSpanGeometry {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  // Build the span shader coordinates for one output. The output origins may
  // be negative; offsets are normalized against the complete desktop's
  // top-left corner. A zero-initialized result tells the renderer to fall back
  // to Crop when the target set cannot describe a multi-output desktop.
  [[nodiscard]] inline WallpaperSpanParams computeWallpaperSpanParams(
      const std::span<const OutputSpanGeometry> outputs, const std::size_t targetIndex
  ) noexcept {
    if (outputs.size() < 2 || targetIndex >= outputs.size()) {
      return {};
    }

    std::int64_t minX = 0;
    std::int64_t minY = 0;
    std::int64_t maxX = 0;
    std::int64_t maxY = 0;
    bool first = true;

    for (const OutputSpanGeometry& output : outputs) {
      if (output.width == 0 || output.height == 0) {
        return {};
      }

      const std::int64_t left = output.x;
      const std::int64_t top = output.y;
      const std::int64_t right = left + static_cast<std::int64_t>(output.width);
      const std::int64_t bottom = top + static_cast<std::int64_t>(output.height);

      if (first) {
        minX = left;
        minY = top;
        maxX = right;
        maxY = bottom;
        first = false;
      } else {
        minX = std::min(minX, left);
        minY = std::min(minY, top);
        maxX = std::max(maxX, right);
        maxY = std::max(maxY, bottom);
      }
    }

    if (first || maxX <= minX || maxY <= minY) {
      return {};
    }

    const OutputSpanGeometry& target = outputs[targetIndex];
    return WallpaperSpanParams{
        .offsetX = static_cast<float>(static_cast<std::int64_t>(target.x) - minX),
        .offsetY = static_cast<float>(static_cast<std::int64_t>(target.y) - minY),
        .monitorWidth = static_cast<float>(target.width),
        .monitorHeight = static_cast<float>(target.height),
        .totalWidth = static_cast<float>(maxX - minX),
        .totalHeight = static_cast<float>(maxY - minY),
    };
  }
} // namespace greeter
