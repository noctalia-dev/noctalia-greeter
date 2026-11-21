#pragma once

#include "render/core/image_decoder.h"

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class RenderBackend;
class SharedTextureCache;

namespace greeter {

  struct WidgetImageAsset {
    std::string reference;
    // Change revision whenever pixels change; managed content hashes supply this at installation.
    std::string revision;
    std::shared_ptr<const LoadedImageFile> pixels;
  };

  // Exact-reference catalog of already decoded, immutable RGBA images. No file access.
  // The publisher owns provenance/decoding checks and must never mutate published pixels.
  class WidgetImageSource {
  public:
    static std::expected<WidgetImageSource, std::string> create(std::span<const WidgetImageAsset> assets);
    const WidgetImageAsset* find(std::string_view reference) const;

  private:
    std::unordered_map<std::string, WidgetImageAsset> m_assets;
  };

  // Consumed during host creation. The source can then be released; cache/backend must
  // outlive mounted hosts. The view owner dispatches the cache and handles GPU recovery.
  struct WidgetImageContext {
    const WidgetImageSource* source = nullptr;
    SharedTextureCache* cache = nullptr;
    RenderBackend* backend = nullptr;
  };

} // namespace greeter
