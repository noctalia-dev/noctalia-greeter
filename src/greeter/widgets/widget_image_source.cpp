#include "greeter/widgets/widget_image_source.h"

#include <cstddef>

namespace greeter {
  namespace {
    constexpr std::size_t kMaxImages = 32;
    constexpr int kMaxImageDimension = 4096;
    constexpr std::size_t kMaxDecodedImageBytes = 64ULL * 1024 * 1024;
    constexpr std::size_t kMaxDecodedTotalBytes = 128ULL * 1024 * 1024;
  } // namespace

  std::expected<WidgetImageSource, std::string> WidgetImageSource::create(std::span<const WidgetImageAsset> assets) {
    if (assets.size() > kMaxImages)
      return std::unexpected("too many managed widget images");
    WidgetImageSource result;
    std::size_t total = 0;
    for (const auto& asset : assets) {
      if (asset.reference.empty() || asset.revision.empty())
        return std::unexpected("managed image reference and revision must be nonempty");
      if (result.m_assets.contains(asset.reference))
        return std::unexpected("duplicate managed image reference");
      const auto& image = asset.pixels;
      if (!image
          || image->width <= 0
          || image->height <= 0
          || image->width > kMaxImageDimension
          || image->height > kMaxImageDimension)
        return std::unexpected("managed image dimensions must be within 1..4096");
      const auto bytes = static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height) * 4;
      if (image->rgba.size() != bytes || bytes > kMaxDecodedImageBytes)
        return std::unexpected("invalid managed RGBA image buffer");
      const bool noSourceDimensions = image->sourceWidth == 0 && image->sourceHeight == 0;
      const bool validSourceDimensions = image->sourceWidth > 0
          && image->sourceHeight > 0
          && image->sourceWidth <= kMaxImageDimension
          && image->sourceHeight <= kMaxImageDimension;
      if (!noSourceDimensions && !validSourceDimensions)
        return std::unexpected("invalid managed image source dimensions");
      if (bytes > kMaxDecodedTotalBytes - total)
        return std::unexpected("managed widget images exceed the decoded byte limit");
      total += bytes;
      result.m_assets.emplace(asset.reference, asset);
    }
    return result;
  }

  const WidgetImageAsset* WidgetImageSource::find(std::string_view reference) const {
    const auto entry = m_assets.find(std::string(reference));
    return entry == m_assets.end() ? nullptr : &entry->second;
  }

} // namespace greeter
