#include "greeter/widgets/widget_image_source.h"
#include "render/codecs/codecs.h"
#include "render/codecs/image_encoder.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <memory>
#include <print>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  std::size_t decodes = 0;
  bool fakeDecode = false;
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  class Decoder final : public ImageDecoder {
    std::expected<DecodedRasterImage, std::string> decode(const std::uint8_t* bytes, std::size_t size) override {
      ++decodes;
      if (!fakeDecode)
        return codecs::decodeRasterImage(bytes, size);
      const auto integer = [](const std::uint8_t* p) {
        return static_cast<int>(
            (static_cast<unsigned>(p[0]) << 24)
            | (static_cast<unsigned>(p[1]) << 16)
            | (static_cast<unsigned>(p[2]) << 8)
            | p[3]
        );
      };
      const int width = integer(bytes + 16), height = integer(bytes + 20);
      return DecodedRasterImage{
          std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4), width,
          height
      };
    }
    std::expected<LoadedImageFile, std::string> loadFile(const std::string&, int, bool) override {
      throw std::runtime_error("loader bypassed descriptor-backed bytes");
    }
  };
  struct Fixture {
    std::filesystem::path state;
    Fixture() {
      char pattern[] = "/tmp/noctalia-widget-images-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      require(created, "mkdtemp failed");
      state = created;
      std::filesystem::create_directory(state / "widget-assets");
      require(::chmod((state / "widget-assets").c_str(), 0700) == 0, "directory mode failed");
      decodes = 0;
      fakeDecode = false;
    }
    ~Fixture() { std::filesystem::remove_all(state); }
    static std::vector<std::uint8_t> png(std::uint8_t color = 128) {
      const std::array<std::uint8_t, 4> pixel{color, 32, 64, 255};
      return encodePng(pixel.data(), 1, 1);
    }
    std::string put(const std::vector<std::uint8_t>& bytes) const {
      gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes.data(), bytes.size());
      require(digest, "hash failed");
      const std::string reference = "widget-assets/" + std::string(digest) + ".png";
      g_free(digest);
      write(state / reference, bytes);
      return reference;
    }
    static void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      out.close();
      require(!out.fail(), "file fixture write failed");
      require(::chmod(path.c_str(), 0600) == 0, "fixture file mode failed");
    }
    static greeter::WidgetInstance sticker(std::string reference, std::string id = "picture") {
      greeter::WidgetInstance instance;
      instance.id = std::move(id);
      instance.type = "sticker";
      instance.settings["image_path"] = std::move(reference);
      return instance;
    }
    auto load(const greeter::WidgetInstance& instance) const {
      return greeter::WidgetImageSource::loadInstalled(state, {std::vector{instance}});
    }
    void rejects(const greeter::WidgetInstance& instance, std::string_view message = {}) const {
      const auto result = load(instance);
      require(!result && !result.error().empty(), "invalid installed image accepted");
      require(result.error().front().message.contains(message), "wrong validation failure");
    }
  };

  void checkReferences() {
    Fixture fixture;
    const auto reference = fixture.put(Fixture::png());
    const auto absolute = (fixture.state / reference).string();
    const auto source = greeter::WidgetImageSource::loadInstalled(
        fixture.state, {std::vector{Fixture::sticker(reference), Fixture::sticker(absolute, "alias")}}
    );
    require(source && decodes == 1, "duplicate image was decoded twice");
    const auto* first = source->find(reference);
    require(
        first && source->find(absolute) == first && first->reference == reference,
        "installed aliases lost shared identity"
    );
    require(first->pixels->width == 1 && first->pixels->rgba.size() == 4, "decoded pixels changed");
    for (const auto& path :
         {"/etc/passwd", "https://example.com/a.png", "~/a.png", "widget-assets/../a.png", "widget-assets/a.png"})
      fixture.rejects(Fixture::sticker(path));
    fixture.rejects(Fixture::sticker((fixture.state / "widget-assets/.." / reference).string()));
    fixture.rejects(Fixture::sticker("/other/" + reference));
    auto uppercase = reference;
    uppercase[14] = 'A';
    fixture.rejects(Fixture::sticker(uppercase));
    require(!source->find(fixture.state.string() + "/./" + reference), "unpublished alias resolved");
    auto disabled = Fixture::sticker("/etc/passwd");
    disabled.enabled = false;
    require(fixture.load(disabled)->empty(), "disabled image was opened");
    disabled.enabled = true;
    disabled.type = "label";
    require(fixture.load(disabled)->empty(), "non-Sticker caused file access");
    require(greeter::WidgetImageSource::loadInstalled("/nonexistent", {})->empty(), "default collection opened state");
    require(fixture.load(Fixture::sticker(""))->empty(), "empty Sticker opened a file");
  }

  void checkProvenance() {
    Fixture fixture;
    const auto reference = fixture.put(Fixture::png());
    const auto path = fixture.state / reference;
    ::chmod(path.c_str(), 0664);
    fixture.rejects(Fixture::sticker(reference), "write");
    ::chmod(path.c_str(), 0644);
    ::chmod((fixture.state / "widget-assets").c_str(), 0777);
    fixture.rejects(Fixture::sticker(reference), "directory");
    ::chmod((fixture.state / "widget-assets").c_str(), 0755);
    ::chmod(fixture.state.c_str(), 0777);
    fixture.rejects(Fixture::sticker(reference), "directory");
    ::chmod(fixture.state.c_str(), 0700);
    std::filesystem::rename(path, fixture.state / "outside.png");
    std::filesystem::create_symlink(fixture.state / "outside.png", path);
    fixture.rejects(Fixture::sticker(reference), "symlink");
    std::filesystem::remove(path);
    std::filesystem::create_hard_link(fixture.state / "outside.png", path);
    fixture.rejects(Fixture::sticker(reference), "single-link");
    std::filesystem::remove(path);
    require(::mkfifo(path.c_str(), 0600) == 0, "FIFO fixture failed");
    fixture.rejects(Fixture::sticker(reference), "regular");
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    fixture.rejects(Fixture::sticker(reference), "regular");
    std::filesystem::remove(path);
    fixture.rejects(Fixture::sticker(reference), "missing");
    std::filesystem::rename(fixture.state / "widget-assets", fixture.state / "real-assets");
    std::filesystem::create_symlink(fixture.state / "real-assets", fixture.state / "widget-assets");
    fixture.rejects(Fixture::sticker(reference), "real");
    std::filesystem::remove(fixture.state / "widget-assets");
    std::filesystem::rename(fixture.state / "real-assets", fixture.state / "widget-assets");
    std::filesystem::create_directory_symlink(fixture.state, fixture.state / "alias");
    const auto linkedState =
        greeter::WidgetImageSource::loadInstalled(fixture.state / "alias", {std::vector{Fixture::sticker(reference)}});
    require(!linkedState, "state directory symlink followed");
    require(decodes == 0, "untrusted file reached decoder");
  }

  void checkContents() {
    Fixture fixture;
    auto bytes = Fixture::png();
    const std::string wrong = "widget-assets/" + std::string(64, 'f') + ".png";
    Fixture::write(fixture.state / wrong, bytes);
    fixture.rejects(Fixture::sticker(wrong), "SHA-256");
    bytes.resize(33);
    auto truncated = fixture.put(bytes);
    fixture.rejects(Fixture::sticker(truncated), "decode");
    bytes[16] = 0;
    bytes[17] = 0;
    bytes[18] = 16;
    bytes[19] = 1;
    const auto tooWide = fixture.put(bytes);
    const auto before = decodes;
    fixture.rejects(Fixture::sticker(tooWide), "dimensions");
    require(decodes == before, "oversized PNG allocated decoded pixels");
    auto invalid = fixture.put({1, 2, 3});
    fixture.rejects(Fixture::sticker(invalid), "PNG");
    std::filesystem::resize_file(fixture.state / invalid, 16ULL * 1024 * 1024 + 1);
    fixture.rejects(Fixture::sticker(invalid), "16 MiB");
  }

  void checkBudgets() {
    Fixture fixture;
    greeter::WidgetCollection collection{std::vector<greeter::WidgetInstance>{}};
    for (unsigned i = 0; i < 33; ++i)
      collection.instances->push_back(
          Fixture::sticker(fixture.put(Fixture::png(static_cast<std::uint8_t>(i))), "picture" + std::to_string(i))
      );
    auto result = greeter::WidgetImageSource::loadInstalled(fixture.state, collection);
    require(!result && result.error().front().message.contains("32") && decodes == 0, "image-count limit bypassed");
    collection.instances->pop_back();
    require(
        greeter::WidgetImageSource::loadInstalled(fixture.state, collection).has_value() && decodes == 32,
        "32-image boundary rejected"
    );
    fakeDecode = true;
    decodes = 0;
    collection.instances->clear();
    auto padded = Fixture::png();
    padded.resize(16ULL * 1024 * 1024);
    for (unsigned i = 0; i < 5; ++i) {
      padded.back() = static_cast<std::uint8_t>(i);
      collection.instances->push_back(Fixture::sticker(fixture.put(padded), "large" + std::to_string(i)));
    }
    result = greeter::WidgetImageSource::loadInstalled(fixture.state, collection);
    require(
        !result && result.error().front().message.contains("64 MiB") && decodes == 4, "encoded aggregate limit bypassed"
    );
    collection.instances->clear();
    decodes = 0;
    auto header = Fixture::png();
    header.resize(33);
    header[18] = 16;
    header[19] = 0;
    header[22] = 16;
    header[23] = 0;
    for (unsigned i = 0; i < 3; ++i) {
      header.back() = static_cast<std::uint8_t>(i);
      collection.instances->push_back(Fixture::sticker(fixture.put(header), "decoded" + std::to_string(i)));
    }
    result = greeter::WidgetImageSource::loadInstalled(fixture.state, collection);
    require(
        !result && result.error().front().message.contains("128 MiB") && decodes == 2,
        "decoded aggregate limit bypassed"
    );
  }
} // namespace

int main() {
  try {
    setImageDecoder(std::make_unique<Decoder>());
    checkReferences();
    checkProvenance();
    checkContents();
    checkBudgets();
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "widget image loader: {}", error.what());
    return 1;
  }
}
