#include "core/ui_phase.h"
#include "greeter/widgets/widget_image_source.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <glib.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace greeter {
  namespace {
    constexpr std::size_t kMaxEncodedBytes = 16ULL * 1024 * 1024;
    constexpr std::size_t kMaxEncodedTotal = 64ULL * 1024 * 1024;
    constexpr std::size_t kMaxDecodedTotal = 128ULL * 1024 * 1024;

    struct Fd {
      int value;
      explicit Fd(int fd) : value(fd) {}
      ~Fd() {
        if (value >= 0)
          ::close(value);
      }
      Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
      Fd(const Fd&) = delete;
      Fd& operator=(const Fd&) = delete;
    };

    bool trusted(const struct stat& st) {
      return (st.st_uid == ::geteuid() || st.st_uid == 0) && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
    }

    std::expected<Fd, std::string> openDirectory(const std::filesystem::path& path) {
      Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
      if (current.value < 0)
        return std::unexpected("cannot open filesystem root");
      for (const auto& component : path.relative_path()) {
        const int next = ::openat(current.value, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
          return std::unexpected(
              "cannot open managed image directory without symlinks: " + std::string(std::strerror(errno))
          );
        ::close(std::exchange(current.value, next));
      }
      struct stat st{};
      if (::fstat(current.value, &st) != 0 || !trusted(st))
        return std::unexpected("managed directory must be root/greeter-owned and not group/world writable");
      return current;
    }

    std::expected<std::vector<std::uint8_t>, std::string> readImage(int directory, const std::string& filename) {
      Fd fd(::openat(directory, filename.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
      struct stat st{};
      if (fd.value < 0 || ::fstat(fd.value, &st) != 0)
        return std::unexpected("managed image is missing, unreadable or a symlink");
      if (!S_ISREG(st.st_mode) || st.st_nlink != 1 || !trusted(st))
        return std::unexpected(
            "managed image must be a single-link regular root/greeter-owned file without group/world write access"
        );
      if (st.st_size <= 0 || static_cast<std::uintmax_t>(st.st_size) > kMaxEncodedBytes)
        return std::unexpected("managed image must contain at most 16 MiB of PNG data");
      std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        const auto count = ::read(fd.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
          continue;
        if (count <= 0)
          return std::unexpected("managed image changed or could not be read");
        offset += static_cast<std::size_t>(count);
      }
      struct stat after{};
      if (::fstat(fd.value, &after) != 0
          || st.st_size != after.st_size
          || st.st_mtim.tv_sec != after.st_mtim.tv_sec
          || st.st_mtim.tv_nsec != after.st_mtim.tv_nsec
          || st.st_ctim.tv_sec != after.st_ctim.tv_sec
          || st.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        return std::unexpected("managed image changed during reading");
      return bytes;
    }

    std::uint32_t bigEndian(const std::uint8_t* bytes) {
      return static_cast<std::uint32_t>(bytes[0]) << 24
          | static_cast<std::uint32_t>(bytes[1]) << 16
          | static_cast<std::uint32_t>(bytes[2]) << 8
          | bytes[3];
    }
  } // namespace

  std::expected<WidgetImageSource, std::vector<WidgetModelIssue>>
  WidgetImageSource::loadInstalled(const std::filesystem::path& stateDirectory, const WidgetCollection& collection) {
    uiAssertNotRendering("WidgetImageSource::loadInstalled");
    const auto fail = [](std::string path, std::string message) {
      return std::unexpected(std::vector<WidgetModelIssue>{{std::move(path), std::move(message)}});
    };
    if (!collection.instances)
      return *create({});
    std::error_code ec;
    const auto state = std::filesystem::absolute(stateDirectory, ec).lexically_normal();
    if (ec)
      return fail("widgets", "cannot resolve managed state directory");
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> references;
    for (const auto& widget : *collection.instances) {
      if (!widget.enabled || widget.type != "sticker")
        continue;
      const auto setting = widget.settings.find("image_path");
      if (setting == widget.settings.end())
        continue;
      const auto* reference = std::get_if<std::string>(&setting->second);
      const std::string field = "widgets.widget." + widget.id + ".settings.image_path";
      if (!reference)
        return fail(field, "image reference must be a string");
      if (reference->empty())
        continue;
      std::string canonical = *reference;
      const auto prefix = (state / "widget-assets").string() + "/";
      if (canonical.starts_with(prefix))
        canonical = "widget-assets/" + canonical.substr(prefix.size());
      constexpr std::string_view directory = "widget-assets/";
      if (!canonical.starts_with(directory)
          || canonical.size() != directory.size() + 64 + 4
          || !canonical.ends_with(".png"))
        return fail(field, "expected widget-assets/<lowercase-sha256>.png or its exact installed absolute path");
      const auto hash = canonical.substr(directory.size(), 64);
      if (!std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        return fail(field, "managed image filename must contain a lowercase SHA-256 hash");
      references[hash].emplace_back(*reference, field);
    }
    if (references.empty())
      return *create({});
    if (references.size() > 32)
      return fail("widgets", "at most 32 distinct managed images are allowed");
    auto stateFd = openDirectory(state);
    if (!stateFd)
      return fail("widgets", stateFd.error());
    Fd directory(::openat(stateFd->value, "widget-assets", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat st{};
    if (directory.value < 0 || ::fstat(directory.value, &st) != 0 || !trusted(st))
      return fail(
          "widgets", "widget-assets must be a real root/greeter-owned directory without group/world write access"
      );

    std::vector<WidgetImageAsset> assets;
    std::size_t encodedTotal = 0, decodedTotal = 0;
    for (const auto& [hash, aliases] : references) {
      auto bytes = readImage(directory.value, hash + ".png");
      const auto& field = aliases.front().second;
      if (!bytes)
        return fail(field, bytes.error());
      if (bytes->size() > kMaxEncodedTotal - encodedTotal)
        return fail(field, "managed images exceed 64 MiB encoded in total");
      encodedTotal += bytes->size();
      constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
      if (bytes->size() < 33
          || !std::equal(signature.begin(), signature.end(), bytes->begin())
          || bigEndian(bytes->data() + 8) != 13
          || std::memcmp(bytes->data() + 12, "IHDR", 4) != 0)
        return fail(field, "managed image must be a PNG with an IHDR header");
      const auto width = bigEndian(bytes->data() + 16), height = bigEndian(bytes->data() + 20);
      if (width == 0 || height == 0 || width > 4096 || height > 4096)
        return fail(field, "managed PNG dimensions must be within 1..4096");
      const auto decodedBytes = static_cast<std::size_t>(width) * height * 4;
      if (decodedBytes > kMaxDecodedTotal - decodedTotal)
        return fail(field, "managed images exceed 128 MiB decoded in total");
      decodedTotal += decodedBytes;
      gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes->data(), bytes->size());
      const bool matches = digest != nullptr && hash == digest;
      g_free(digest);
      if (!matches)
        return fail(field, "managed PNG does not match its SHA-256 filename");
      auto image = decodeRasterImage(bytes->data(), bytes->size());
      if (!image)
        return fail(field, "managed PNG decode failed: " + image.error());
      if (image->width != static_cast<int>(width)
          || image->height != static_cast<int>(height)
          || image->pixels.size() != decodedBytes)
        return fail(field, "decoded PNG dimensions/buffer differ from its bounded header");
      auto pixels = std::make_shared<LoadedImageFile>(
          LoadedImageFile{std::move(image->pixels), image->width, image->height, image->width, image->height}
      );
      assets.push_back({"widget-assets/" + hash + ".png", hash, std::move(pixels)});
    }
    auto result = create(assets);
    if (!result)
      return fail("widgets", result.error());
    for (const auto& [hash, aliases] : references)
      for (const auto& [reference, field] : aliases) {
        (void)field;
        if (reference != "widget-assets/" + hash + ".png")
          result->m_aliases.emplace(reference, "widget-assets/" + hash + ".png");
      }
    return std::move(*result);
  }
} // namespace greeter
