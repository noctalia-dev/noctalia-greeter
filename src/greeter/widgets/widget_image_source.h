#pragma once

#include "greeter/widgets/widget_model.h"
#include "render/core/image_decoder.h"

#include <expected>
#include <filesystem>
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

  // Exact-reference catalog of decoded, immutable RGBA images. create()/find() do no file access.
  // External publishers own provenance/decoding checks and must never mutate published pixels.
  class WidgetImageSource {
  public:
    static std::expected<WidgetImageSource, std::string> create(std::span<const WidgetImageAsset> assets);
    // Read only referenced, enabled installed PNGs; validate before decoding/publishing.
    static std::expected<WidgetImageSource, std::vector<WidgetModelIssue>>
    loadInstalled(const std::filesystem::path& stateDirectory, const WidgetCollection& collection);
    bool empty() const noexcept { return m_assets.empty(); }
    const WidgetImageAsset* find(std::string_view reference) const;

  private:
    std::unordered_map<std::string, WidgetImageAsset> m_assets;
    // Only exact absolute references explicitly admitted by loadInstalled().
    std::unordered_map<std::string, std::string> m_aliases;
  };

  // Consumed during host creation. The source can then be released; cache/backend must
  // outlive mounted hosts. The view owner dispatches the cache and handles GPU recovery.
  struct WidgetImageContext {
    const WidgetImageSource* source = nullptr;
    SharedTextureCache* cache = nullptr;
    RenderBackend* backend = nullptr;
  };

} // namespace greeter
