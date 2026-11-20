#pragma once
#include "core/log.h"
#include "render/core/image_decoder.h"
#include "render/core/texture_manager.h"

inline TextureHandle
loadGreeterTexture(TextureManager& textures, const std::string& path, int targetSize, bool mipmap) {
  const auto image = loadImageFile(path, targetSize);
  if (!image) {
    constexpr Logger log("greeter-image");
    log.warn("failed to load '{}': {}", path, image.error());
    return {};
  }
  auto handle = textures.loadFromRgba(image->rgba.data(), image->width, image->height, mipmap);
  handle.sourceWidth = image->sourceWidth;
  handle.sourceHeight = image->sourceHeight;
  return handle;
}
