#include "greeter/widgets/sticker_widget.h"

#include <sticker_widget.h>
#include <utility>

namespace greeter {

  StickerWidget::StickerWidget(std::shared_ptr<const LoadedImageFile> pixels, float opacity)
      : m_pixels(std::move(pixels)) {
    setHitTestVisible(false);
    setOpacity(opacity);
    auto sticker = std::make_unique<::StickerWidget>();
    m_sticker = sticker.get();
    addChild(std::move(sticker));
  }

  std::expected<std::unique_ptr<StickerWidget>, std::string> StickerWidget::create(
      const WidgetImageAsset* asset, SharedTextureCache* cache, RenderBackend* backend, float opacity
  ) {
    if (asset != nullptr && (cache == nullptr || backend == nullptr))
      return std::unexpected("managed image texture context is unavailable");
    auto widget = std::unique_ptr<StickerWidget>(new StickerWidget(asset ? asset->pixels : nullptr, opacity));
    if (asset != nullptr) {
      widget->m_lease = cache->acquirePrepared(
          {.source = "greeter-widget:" + asset->reference, .revision = asset->revision, .mipmap = true}, asset->pixels,
          backend, [content = widget.get()] { content->markLayoutDirty(); }
      );
      if (widget->m_lease.empty())
        return std::unexpected("managed image texture request was rejected");
    }
    return widget;
  }

  bool StickerWidget::hasPendingContent() const noexcept {
    return !m_lease.empty() && !m_lease.failed() && !m_lease.texture().valid();
  }

  void StickerWidget::layoutAtScale(Renderer& renderer, float scale) {
    // The cache uploads from dispatch; layout only binds its current externally owned texture.
    if (m_pixels)
      m_sticker->setExternalTexture(renderer, m_lease.texture(), m_pixels->width, m_pixels->height);
    m_sticker->setContentScale(scale);
    m_sticker->layout(renderer);
    setSize(m_sticker->width(), m_sticker->height());
  }

} // namespace greeter
