#include "greeter/widgets/widget_image_source.h"

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  std::shared_ptr<LoadedImageFile> pixels(int width = 2, int height = 3) {
    auto image = std::make_shared<LoadedImageFile>();
    image->width = width;
    image->height = height;
    image->rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    return image;
  }
  void rejects(const greeter::WidgetImageAsset& asset) {
    require(!greeter::WidgetImageSource::create(std::array{asset}), "invalid managed image admitted");
  }
} // namespace

int main() {
  try {
    auto image = pixels();
    const greeter::WidgetImageAsset asset{"widget-assets/example.png", "revision1", image};
    const auto source = greeter::WidgetImageSource::create(std::array{asset});
    require(source.has_value(), "valid managed image rejected");
    const auto* found = source->find(asset.reference);
    require(found && found->pixels == image && found->revision == "revision1", "managed image identity changed");
    for (const auto reference : std::array<std::string_view, 7>{
             "", "/tmp/example.png", "../example.png", "widget-assets/../example.png", "https://example.com/image.png",
             "Widget-assets/example.png", "widget-assets/example.png "
         })
      require(source->find(reference) == nullptr, "unpublished reference resolved or acquired an alias");
    require(greeter::WidgetImageSource::create({}).has_value(), "empty managed source rejected");
    rejects({"", "revision1", image});
    rejects({asset.reference, "", image});
    rejects({asset.reference, "revision1", nullptr});
    require(!greeter::WidgetImageSource::create(std::array{asset, asset}), "duplicate reference admitted");
    for (const auto dimension : {0, -1, 4097, std::numeric_limits<int>::max()}) {
      auto malformed = std::make_shared<LoadedImageFile>();
      malformed->width = dimension;
      malformed->height = 1;
      rejects({asset.reference, "revision1", malformed});
      malformed->height = dimension;
      malformed->width = 1;
      rejects({asset.reference, "revision1", malformed});
    }
    auto malformed = pixels();
    malformed->rgba.pop_back();
    rejects({asset.reference, "revision1", malformed});
    malformed->rgba.push_back(0);
    malformed->rgba.push_back(0);
    rejects({asset.reference, "revision1", malformed});
    malformed = pixels();
    malformed->sourceWidth = 2;
    rejects({asset.reference, "revision1", malformed});
    malformed->sourceHeight = 4097;
    rejects({asset.reference, "revision1", malformed});
    malformed->sourceHeight = 3;
    require(
        greeter::WidgetImageSource::create(
            std::array{greeter::WidgetImageAsset{asset.reference, "r", malformed}}
        ).has_value(),
        "valid source dimensions rejected"
    );

    std::vector<greeter::WidgetImageAsset> many;
    many.reserve(33);
    for (int i = 0; i < 32; ++i)
      many.push_back({"image" + std::to_string(i), "r", image});
    require(greeter::WidgetImageSource::create(many).has_value(), "32 managed image identities rejected");
    many.push_back({"overflow", "r", image});
    require(!greeter::WidgetImageSource::create(many), "managed image count limit bypassed");

    // Distinct cache identities each consume the aggregate budget, even when CPU pixels are shared.
    auto maximum = pixels(4096, 4096);
    std::array bounded{
        greeter::WidgetImageAsset{"first", "r", maximum},
        greeter::WidgetImageAsset{"second", "r", maximum},
        greeter::WidgetImageAsset{"third", "r", image},
    };
    require(greeter::WidgetImageSource::create(std::span(bounded).first(2)).has_value(), "128 MiB boundary rejected");
    require(!greeter::WidgetImageSource::create(bounded), "aggregate decoded budget bypassed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget-image-source-test: {}", error.what());
    return 1;
  }
}
