#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Upper bound on a WebP canvas we will decode, in RGBA bytes. A WebP VP8X header
// can declare a canvas far larger than its (small) file size, and libwebp would
// allocate the full canvas (twice, for animation) before any downstream check,
// so an attacker-sized canvas is an OOM vector. 256 MiB (~8192x8192 RGBA) is far
// above any real display while bounding the allocation.
inline constexpr std::size_t kMaxWebpCanvasBytes = 256ULL * 1024 * 1024;

struct DecodedRasterImage {
  std::vector<std::uint8_t> pixels;
  int width = 0;
  int height = 0;
};

[[nodiscard]] std::optional<DecodedRasterImage>
decodeRasterImage(const std::uint8_t* data, std::size_t size, std::string* errorMessage = nullptr);

struct DecodedRasterFrame {
  std::vector<std::uint8_t> rgba; // canvas-size, RGBA8 non-premul
  std::uint32_t durationMs = 0;
};

struct DecodedRasterAnimation {
  int width = 0;
  int height = 0;
  std::vector<DecodedRasterFrame> frames;
  bool truncated = false; // hit cap; later frames omitted
};

// Decode animated GIF frames; caps set truncated and return partial frames.
[[nodiscard]] std::optional<DecodedRasterAnimation> decodeAnimatedGif(
    const std::uint8_t* data, std::size_t size, int maxFrames, std::size_t maxRgbaBytes,
    std::string* errorMessage = nullptr
);
