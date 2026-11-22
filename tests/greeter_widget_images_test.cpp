#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_surface.h"
#include "greeter/greeter_window.h"
#include "greeter/library_runtime.h"
#include "greeter/resources.h"
#include "greeter/widgets/widget_host.h"
#include "greeter/widgets/widget_image_source.h"
#include "greeter/widgets/widget_layer.h"
#include "render/codecs/image_encoder.h"
#include "render/core/shared_texture_cache.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "ui/controls/image.h"
#include "wayland/wayland_client.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <memory>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <wayland-client.h>

namespace {
  using namespace std::chrono_literals;
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  void dispatchFor(WaylandClient& client, SharedTextureCache& cache, std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    auto* display = client.display();
    while (std::chrono::steady_clock::now() < end) {
      drainGreeterUiWork();
      require(wl_display_dispatch_pending(display) >= 0, "dispatch failed");
      if (wl_display_prepare_read(display) != 0)
        continue;
      require(client.flush() >= 0, "flush failed");
      std::vector<pollfd> fds{{.fd = wl_display_get_fd(display), .events = POLLIN, .revents = 0}};
      const auto textureIndex = cache.addPollFds(fds);
      const int ready = poll(fds.data(), static_cast<nfds_t>(fds.size()), 10);
      if (ready > 0 && (fds[0].revents & POLLIN) != 0)
        require(wl_display_read_events(display) >= 0, "read failed");
      else {
        wl_display_cancel_read(display);
        require(ready >= 0, "poll failed");
      }
      cache.dispatch(fds, textureIndex);
    }
    drainGreeterUiWork();
  }
  Image* image(Node* root) {
    if (auto* result = dynamic_cast<Image*>(root))
      return result;
    for (const auto& child : root->children())
      if (auto* result = image(child.get()))
        return result;
    return nullptr;
  }
  greeter::WidgetLayer* layer(GreeterSurface& surface) {
    for (const auto& child : surface.sceneRoot()->children())
      if (auto* result = dynamic_cast<greeter::WidgetLayer*>(child.get()))
        return result;
    return nullptr;
  }
  greeter::WidgetInstance sticker(std::string id, std::string reference) {
    greeter::WidgetInstance result;
    result.id = std::move(id);
    result.type = "sticker";
    result.settings = {{"image_path", std::move(reference)}, {"background", false}};
    result.boxWidth = 160;
    result.boxHeight = 160;
    result.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::TopLeft, 0, 100};
    return result;
  }

  void testImages() {
    initializeGreeterLibrary();
    const auto state = greeter::greeterConfPath().parent_path();
    std::filesystem::create_directory(state / "widget-assets");
    require(::chmod((state / "widget-assets").c_str(), 0700) == 0, "directory mode failed");
    constexpr std::array<std::uint8_t, 16> pixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const auto png = encodePng(pixels.data(), 2, 2);
    gchar* hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, png.data(), png.size());
    require(hash, "hash failed");
    const std::string reference = "widget-assets/" + std::string(hash) + ".png";
    g_free(hash);
    const auto path = state / reference;
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    require(::chmod(path.c_str(), 0600) == 0, "file mode failed");
    auto first = sticker("first", reference), second = sticker("second", path.string());
    second.transform = {0.2, true, false};
    second.placement = greeter::WidgetAnchorPlacement{greeter::WidgetAnchor::TopLeft, 240, 100};
    const greeter::WidgetCollection collection{std::vector{first, second}};
    auto source = greeter::WidgetImageSource::loadInstalled(state, collection);
    require(source.has_value(), "installed PNG load failed");
    greeter::config::GreeterConfigFile config;
    config.widgets = collection;
    config.clockEnabled = false;
    require(greeter::config::writeConfig(greeter::greeterConfPath(), config), "config fixture failed");
    WaylandClient client;
    require(client.connect(), "connect failed");
    require(wl_display_roundtrip(client.display()) >= 0, "initial roundtrip failed");
    const auto outputs = client.readyOutputsSorted();
    require(outputs.size() == 2, "two outputs required");
    GlSharedContext gl;
    gl.initialize(client.display());
    initializeGreeterAssets();
    RenderContext context;
    context.initialize(gl);
    SharedTextureCache cache;
    cache.initialize(&gl);
    const greeter::WidgetImageContext images{.source = &*source, .cache = &cache, .backend = &context.backend()};
    GreeterModel model;
    model.initialize();
    GreetdClient greetd;
    GreeterAuthController auth(model, greetd);
    auto a = std::make_unique<GreeterSurface>(model, auth, 1);
    a->setWidgetImages(images);
    a->initialize(&context);
    auto aw = std::make_unique<GreeterWindow>(client, gl, context, *a);
    a->setWindow(aw.get());
    aw->bindOutput(outputs[0]->output);
    require(aw->createSurface(), "first view failed");
    aw->setSceneReady(true);
    auto b = std::make_unique<GreeterSurface>(model, auth, 2);
    b->setWidgetImages(images);
    b->initialize(&context);
    auto bw = std::make_unique<GreeterWindow>(client, gl, context, *b);
    b->setWindow(bw.get());
    bw->bindOutput(outputs[1]->output);
    require(bw->createSurface(), "second view failed");
    bw->setSceneReady(true);
    dispatchFor(client, cache, 250ms);
    require(layer(*a) && layer(*b), "image layers did not mount");
    const auto texture = image(layer(*a)->children()[0].get())->textureId();
    require(texture != 0 && cache.preparedBytes() == pixels.size(), "prepared pixels missing or duplicated");
    for (auto* scene : {a.get(), b.get()})
      for (const auto& host : layer(*scene)->children())
        require(
            image(host.get())->hasImage() && image(host.get())->textureId() == texture,
            "cross-view image upload not shared"
        );
    const auto resident = cache.residentBytes();
    require(resident > 0, "texture never uploaded");
    aw.reset();
    a.reset();
    require(cache.residentBytes() == resident, "removing one view evicted the other view's image");
    bw.reset();
    b.reset();
    require(cache.residentBytes() == 0 && cache.preparedBytes() == 0, "last view leaked cache residency");

    auto retained = greeter::WidgetHost::create(first, images);
    require(retained.has_value(), "recovery host failed");
    ScaledRenderer renderer(context, 1);
    dispatchFor(client, cache, 100ms);
    (*retained)->layout(renderer);
    require(image(retained->get())->hasImage(), "recovery host never acquired image");
    // Lose the graphics share group with no native targets alive. Keep the lease
    // and prepared pixels, then verify re-upload without any filesystem fallback.
    std::filesystem::remove(path);
    cache.abandonGpuResources();
    require(cache.residentBytes() == 0 && cache.preparedBytes() == pixels.size(), "GPU loss discarded CPU pixels");
    context.prepareForGraphicsReset();
    gl.recreateRootContext();
    context.restoreAfterGraphicsReset(gl);
    cache.reloadResidentTextures();
    context.finishGraphicsResetRecovery();
    dispatchFor(client, cache, 100ms);
    (*retained)->layout(renderer);
    require(
        image(retained->get())->hasImage() && cache.residentBytes() == resident,
        "prepared image did not recover after GPU loss"
    );
    retained->reset();
    require(cache.preparedBytes() == 0 && cache.residentBytes() == 0, "recovered lease leaked residency");
    std::puts("widget images: installed assets, sharing, recovery and teardown passed");
  }
} // namespace

int main() {
  try {
    testImages();
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget images: {}", error.what());
    return 1;
  }
}
