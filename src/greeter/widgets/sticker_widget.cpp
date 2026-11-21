#include "greeter/widgets/sticker_widget.h"

#include "ui/controls/image.h"

#include <algorithm>
#include <utility>

namespace greeter {

  StickerWidget::StickerWidget(std::shared_ptr<const LoadedImageFile> pixels, float opacity)
      : m_pixels(std::move(pixels)) {
    setHitTestVisible(false);
    setOpacity(opacity);
    auto image = std::make_unique<Image>();
    m_image = image.get();
    image->setHitTestVisible(false);
    image->setFit(ImageFit::Contain);
    addChild(std::move(image));
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
    m_image->setExternalTexture(renderer, m_lease.texture());
    const float baseSize = 200.0F * scale;
    const float width = m_pixels ? static_cast<float>(m_pixels->width) : 1.0F;
    const float height = m_pixels ? static_cast<float>(m_pixels->height) : 1.0F;
    const float longest = std::max(width, height);
    const float fittedWidth = baseSize * width / longest;
    const float fittedHeight = baseSize * height / longest;
    m_image->setSize(fittedWidth, fittedHeight);
    setSize(fittedWidth, fittedHeight);
  }

} // namespace greeter
