#pragma once

#include "greeter/widgets/widget_content.h"
#include "greeter/widgets/widget_image_source.h"
#include "render/core/shared_texture_cache.h"

#include <expected>
#include <memory>
#include <string>

class StickerWidget;

namespace greeter {

  class StickerWidget final : public WidgetContent {
  public:
    // Null asset is an intentionally empty sticker. Nonempty assets need a cache and live backend.
    static std::expected<std::unique_ptr<StickerWidget>, std::string>
    create(const WidgetImageAsset* asset, SharedTextureCache* cache, RenderBackend* backend, float opacity);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool hasPendingContent() const noexcept override;

  private:
    StickerWidget(std::shared_ptr<const LoadedImageFile> pixels, float opacity);
    ::StickerWidget* m_sticker = nullptr;
    std::shared_ptr<const LoadedImageFile> m_pixels;
    SharedTextureCache::Lease m_lease;
  };

} // namespace greeter
