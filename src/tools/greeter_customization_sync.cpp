#include "tools/greeter_customization_sync.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_preferences.h"
#include "greeter/widgets/widget_image_source.h"
#include "render/codecs/codecs.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <initializer_list>
#include <ranges>
#include <sys/stat.h>
#include <toml++/toml.hpp>
#include <unistd.h>
#include <utility>

namespace greeter::secure_sync {
  namespace {
    constexpr Logger kLog("greeter-customization-sync");

    bool hashString(std::string_view hash) {
      return hash.size() == 64
          && std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }
    bool fail(std::string message, std::string& error) {
      error = std::move(message);
      return false;
    }
    bool writePrivateToml(const std::filesystem::path& path, const toml::table& root, std::string& error) {
      std::ofstream out(path);
      out << toml::toml_formatter{root};
      out.close();
      return !out.fail() || fail("failed to normalize private customization snapshot", error);
    }

    bool allowedKeys(const toml::table& table, std::initializer_list<std::string_view> allowed, std::string& error) {
      for (const auto& [key, node] : table) {
        (void)node;
        if (std::ranges::find(allowed, key.str()) == allowed.end())
          return fail("unsupported staged key: " + std::string(key.str()), error);
      }
      return true;
    }
    bool validateWallpaperTypes(const toml::table& wallpaper, std::string& error) {
      if (!allowedKeys(wallpaper, {"path", "fill_mode", "fill_color", "blur", "tint"}, error))
        return false;
      for (const auto* key : {"path", "fill_mode", "fill_color"})
        if (wallpaper.contains(key) && !wallpaper[key].is_string())
          return fail("staged wallpaper strings have an invalid type", error);
      for (const auto* key : {"blur", "tint"})
        if (wallpaper.contains(key)) {
          const auto number = wallpaper[key].value<double>();
          if (!wallpaper[key].is_number() || !number || !std::isfinite(*number) || *number < 0 || *number > 1)
            return fail("staged wallpaper effects must be finite numbers within 0..1", error);
        }
      return true;
    }
    bool validateAppearanceTypes(const toml::table& root, std::string& error) {
      const auto* appearance = root["appearance"].as_table();
      if (!appearance
          || !allowedKeys(
              *appearance,
              {"scheme", "theme_mode", "corner_radius_scale", "font_family", "palette", "wallpaper", "wallpapers"},
              error
          ))
        return fail(error.empty() ? "staged appearance must be a table" : error, error);
      for (const auto* key : {"scheme", "theme_mode", "font_family"})
        if (appearance->contains(key) && !(*appearance)[key].is_string())
          return fail("staged appearance strings have an invalid type", error);
      if (appearance->contains("corner_radius_scale")) {
        const auto number = (*appearance)["corner_radius_scale"].value<double>();
        if (!(*appearance)["corner_radius_scale"].is_number()
            || !number
            || !std::isfinite(*number)
            || *number < 0
            || *number > 2)
          return fail("staged corner radius scale must be within 0..2", error);
      }
      const auto* palette = (*appearance)["palette"].as_table();
      if (!palette)
        return fail("staged palette must be a table", error);
      for (const auto& [key, value] : *palette) {
        (void)key;
        if (!value.is_string())
          return fail("staged palette values must be strings", error);
      }
      if (appearance->contains("wallpaper")) {
        const auto* wallpaper = (*appearance)["wallpaper"].as_table();
        if (!wallpaper || !validateWallpaperTypes(*wallpaper, error))
          return fail(error.empty() ? "staged wallpaper must be a table" : error, error);
      }
      if (appearance->contains("wallpapers")) {
        const auto* wallpapers = (*appearance)["wallpapers"].as_table();
        if (!wallpapers)
          return fail("staged wallpapers must be a table", error);
        for (const auto& [key, value] : *wallpapers) {
          (void)key;
          if (!value.is_table() || !validateWallpaperTypes(*value.as_table(), error))
            return fail(error.empty() ? "staged output wallpaper must be a table" : error, error);
        }
      }
      return true;
    }
    bool copyToFd(const std::filesystem::path& source, int fd) {
      std::ifstream input(source, std::ios::binary);
      if (!input)
        return false;
      std::array<char, 64U * 1024U> bytes{};
      while (input) {
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        std::size_t written = 0;
        const auto count = static_cast<std::size_t>(input.gcount());
        while (written < count) {
          const auto result = ::write(fd, bytes.data() + written, count - written);
          if (result < 0 && errno == EINTR)
            continue;
          if (result <= 0)
            return false;
          written += static_cast<std::size_t>(result);
        }
      }
      return input.eof();
    }

    // Old wallpaper inodes stay on the same filesystem, retaining ownership and
    // permissions for rollback. Sync itself commits through the existing atomic writer.
    class InstallRollback {
    public:
      InstallRollback(std::string& error, std::filesystem::path state, mode_t mode)
          : m_error(error), m_state(std::move(state)), m_mode(mode) {}
      ~InstallRollback() {
        for (const auto& entry : std::views::reverse(m_files)) {
          if (m_committed) {
            if (!entry.backup.empty())
              ::unlink(entry.backup.c_str());
          } else if (entry.backup.empty()) {
            if (::unlink(entry.path.c_str()) != 0 && errno != ENOENT)
              m_error += "; rollback failed for " + entry.path.string();
          } else if (::rename(entry.backup.c_str(), entry.path.c_str()) != 0) {
            m_error += "; rollback failed for " + entry.path.string();
          }
        }
        if (!m_committed) {
          if (!newImageDirectory.empty())
            ::rmdir(newImageDirectory.c_str());
          if (::chmod(m_state.c_str(), m_mode) != 0)
            m_error += "; failed to restore state directory permissions";
        }
      }
      bool watch(const std::filesystem::path& path) {
        struct stat st{};
        if (::lstat(path.c_str(), &st) != 0) {
          if (errno != ENOENT)
            return fail("failed to inspect transaction destination: " + path.string(), m_error);
          m_files.push_back({path, {}});
          return true;
        }
        if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid())
          return fail("refusing to replace a nonregular or administrator-owned destination: " + path.string(), m_error);
        std::string backup = (path.parent_path() / ".customization-rollback.XXXXXX").string();
        const int fd = ::mkstemp(backup.data());
        if (fd < 0)
          return fail("failed to create transaction backup", m_error);
        ::close(fd);
        if (::rename(path.c_str(), backup.c_str()) != 0) {
          ::unlink(backup.c_str());
          return fail("failed to preserve previous wallpaper", m_error);
        }
        m_files.push_back({path, backup});
        return true;
      }
      void commit() { m_committed = true; }
      std::filesystem::path newImageDirectory;

    private:
      struct Entry {
        std::filesystem::path path, backup;
      };
      std::vector<Entry> m_files;
      std::string& m_error;
      std::filesystem::path m_state;
      mode_t m_mode;
      bool m_committed = false;
    };

    WidgetCollection imageCheckCollection(const WidgetCollection& collection) {
      auto checked = collection;
      if (checked.instances)
        for (auto& widget : *checked.instances)
          if (widget.type == "sticker")
            widget.enabled = true;
      return checked;
    }

    bool safeImageDirectory(const std::filesystem::path& directory, InstallRollback& rollback, std::string& error) {
      struct stat st{};
      if (::lstat(directory.c_str(), &st) != 0) {
        if (errno != ENOENT || ::mkdir(directory.c_str(), 0755) != 0)
          return fail("cannot create managed image directory", error);
        rollback.newImageDirectory = directory;
        if (::lstat(directory.c_str(), &st) != 0)
          return fail("cannot inspect new image directory", error);
      }
      if (!S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        return fail("managed image directory has unsafe ownership, type or permissions", error);
      return true;
    }

    void pruneImages(const std::filesystem::path& state) {
      config::clearConfigDiagnostics();
      const auto admin = config::loadConfig(state / "greeter.toml");
      const auto sync = config::loadSync(state / "sync.toml");
      if (!config::configDiagnostics().empty())
        return;
      std::unordered_set<std::string> retained;
      for (const auto* collection : {&admin.widgets, &sync.widgets})
        if (*collection && (*collection)->instances)
          for (const auto& widget : *(*collection)->instances) {
            if (widget.type != "sticker")
              continue;
            const auto setting = widget.settings.find("image_path");
            if (setting == widget.settings.end())
              continue;
            if (const auto* path = std::get_if<std::string>(&setting->second))
              retained.emplace(std::filesystem::path(*path).filename().string());
          }
      std::error_code ec;
      std::filesystem::directory_iterator it(state / "widget-assets", ec), end;
      while (!ec && it != end) {
        const auto path = it->path();
        const auto name = path.filename().string();
        struct stat st{};
        if (name.size() == 68
            && name.ends_with(".png")
            && hashString(std::string_view(name).substr(0, 64))
            && !retained.contains(name)
            && ::lstat(path.c_str(), &st) == 0
            && S_ISREG(st.st_mode)
            && st.st_uid == ::geteuid()
            && st.st_nlink == 1) {
          if (::unlink(path.c_str()) != 0)
            kLog.warn("could not remove unused widget image {}", path.string());
        }
        it.increment(ec);
      }
    }
  } // namespace

  bool isWidgetImageFileName(std::string_view name) {
    return name.size() == 75 && name.starts_with("widget-") && name.ends_with(".png") && hashString(name.substr(7, 64));
  }

  bool prepareCustomization(const std::filesystem::path& staging, CustomizationUpdate& update, std::string& error) {
    update = {};
    toml::table root;
    try {
      root = toml::parse_file((staging / "sync.toml").string());
    } catch (const toml::parse_error& e) {
      return fail("invalid staged sync.toml: " + std::string(e.description()), error);
    }
    for (const auto& [key, node] : root) {
      (void)node;
      if (key.str() != "appearance" && key.str() != "layout" && key.str() != "widgets" && key.str() != "assets")
        return fail("constrained sync cannot change table: " + std::string(key.str()), error);
    }
    if (!validateAppearanceTypes(root, error))
      return false;
    if (const auto* layout = root["layout"].as_table(); layout && layout->contains("clock"))
      return fail("staged layout.clock is legacy; use ordinary Clock widget instances", error);
    std::unordered_set<std::string> manifest;
    if (root.contains("assets")) {
      const auto* assets = root["assets"].as_table();
      const auto* images = assets ? (*assets)["images"].as_array() : nullptr;
      if (!assets || assets->size() != 1 || !images || images->size() > 32)
        return fail("assets must contain only an images array of at most 32 PNG basenames", error);
      for (const auto& item : *images) {
        const auto name = item.value_exact<std::string>();
        if (!name || !isWidgetImageFileName(*name) || !manifest.emplace(*name).second)
          return fail("assets.images contains an invalid or duplicate PNG basename", error);
      }
      root.erase("assets");
      if (!writePrivateToml(staging / "sync.toml", root, error))
        return false;
    }
    config::clearConfigDiagnostics();
    const auto sync = config::loadSync(staging / "sync.toml");
    if (!config::configDiagnostics().empty())
      return fail("invalid staged customization: " + config::configDiagnostics().front().message, error);
    update.layout = sync.layout;
    update.widgets = sync.widgets;
    if (update.layout
        && update.layout->components[0].width
        && (*update.layout->components[0].width < 280 || *update.layout->components[0].width > 640))
      return fail("staged login width must be within 280..640", error);
    std::unordered_set<std::string> referenced;
    if (update.widgets && update.widgets->instances)
      for (const auto& widget : *update.widgets->instances) {
        if (widget.type != "sticker")
          continue;
        const auto setting = widget.settings.find("image_path");
        if (setting == widget.settings.end())
          continue;
        const auto& ref = std::get<std::string>(setting->second);
        if (ref.empty())
          continue;
        if (!ref.starts_with("widget-assets/")
            || ref.size() != 82
            || !ref.ends_with(".png")
            || !hashString(std::string_view(ref).substr(14, 64)))
          return fail("staged image_path must be widget-assets/<lowercase-sha256>.png", error);
        referenced.emplace("widget-" + ref.substr(14));
      }
    std::unordered_set<std::string> files;
    std::error_code listingError;
    std::filesystem::directory_iterator it(staging, listingError), end;
    while (!listingError && it != end) {
      const auto name = it->path().filename().string();
      if (isWidgetImageFileName(name))
        files.emplace(name);
      it.increment(listingError);
    }
    if (listingError)
      return fail("cannot enumerate private snapshot", error);
    if (manifest != referenced || manifest != files)
      return fail("assets.images, Sticker references and staged PNG files must match exactly", error);
    if (!manifest.empty()) {
      std::error_code ec;
      if (::mkdir((staging / "widget-assets").c_str(), 0700) != 0)
        return fail("cannot prepare private widget-assets directory", error);
      for (const auto& name : manifest) {
        const auto hash = name.substr(7, 64);
        std::filesystem::rename(staging / name, staging / "widget-assets" / (hash + ".png"), ec);
        if (ec)
          return fail("cannot normalize private widget image", error);
        update.imageHashes.push_back(hash);
      }
      codecs::installImageDecoder();
      const auto images = WidgetImageSource::loadInstalled(staging, imageCheckCollection(*update.widgets));
      if (!images)
        return fail(images.error().front().path + ": " + images.error().front().message, error);
    }
    return true;
  }

  bool installCustomization(
      const std::filesystem::path& staging, CustomizationUpdate update,
      const std::unordered_set<std::string>& wallpaperFiles, std::string& error
  ) {
    const auto state = appearance::syncedDataDirectory();
    struct stat st{};
    if (::lstat(state.c_str(), &st) != 0
        || !S_ISDIR(st.st_mode)
        || st.st_uid != ::geteuid()
        || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
      return fail("customization state directory has unsafe ownership, type or permissions", error);
    InstallRollback rollback(error, state, st.st_mode & 07777);
    // A legacy-key migration may create sync.toml before the final merge fails.
    if (::lstat((state / "sync.toml").c_str(), &st) != 0) {
      if (errno != ENOENT || !rollback.watch(state / "sync.toml"))
        return false;
    }
    if (!update.imageHashes.empty() && !safeImageDirectory(state / "widget-assets", rollback, error))
      return false;
    for (const auto& hash : update.imageHashes) {
      const auto name = hash + ".png";
      const auto destination = state / "widget-assets" / name;
      if (::lstat(destination.c_str(), &st) == 0) {
        WidgetInstance image;
        image.id = "existing";
        image.type = "sticker";
        image.settings["image_path"] = "widget-assets/" + name;
        const auto existing = WidgetImageSource::loadInstalled(state, {std::vector{image}});
        if (!existing)
          return fail("existing managed image is unsafe or corrupt: " + destination.string(), error);
        continue;
      }
      if (errno != ENOENT || !rollback.watch(destination))
        return fail("cannot inspect managed image destination", error);
      std::string temporary = (state / "widget-assets" / ".widget.tmp.XXXXXX").string();
      const int fd = ::mkstemp(temporary.data());
      if (fd < 0)
        return fail("cannot create managed image temporary", error);
      const bool ready = copyToFd(staging / "widget-assets" / name, fd) && ::fchmod(fd, 0644) == 0 && ::fsync(fd) == 0;
      ::close(fd);
      const bool installed = ready && ::rename(temporary.c_str(), destination.c_str()) == 0;
      ::unlink(temporary.c_str());
      if (!installed)
        return fail("failed to install managed PNG", error);
    }
    if (update.widgets && update.widgets->instances)
      for (auto& widget : *update.widgets->instances)
        if (widget.type == "sticker")
          if (auto setting = widget.settings.find("image_path"); setting != widget.settings.end())
            if (auto* ref = std::get_if<std::string>(&setting->second); ref && !ref->empty())
              *ref = (state / *ref).string();
    for (const auto& name : wallpaperFiles)
      if (!rollback.watch(state / name))
        return false;
    if (!appearance::installFromStaging(staging, error)
        || !appearance::applySyncedGreeterPreferences(staging, false, error, update.layout, update.widgets))
      return false;
    rollback.commit();
    pruneImages(state);
    return true;
  }
} // namespace greeter::secure_sync
