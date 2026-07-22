#include "greeter/appearance_sync.h"

#include "greeter/greeter_preferences.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace greeter::appearance {

  namespace {

    constexpr mode_t kSyncedDirMode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    constexpr mode_t kSyncedFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    [[nodiscard]] bool isWallpaperFileName(std::string_view name) {
      if (name == kWallpaperBaseName) {
        return true;
      }
      constexpr std::string_view kPrefix = "wallpaper.";
      return name.size() > kPrefix.size() && name.substr(0, kPrefix.size()) == kPrefix;
    }

    [[nodiscard]] bool setMode(const std::filesystem::path& path, mode_t mode, std::string& errorOut) {
      if (::chmod(path.c_str(), mode) != 0) {
        errorOut = std::string("chmod failed for '") + path.string() + "': " + std::strerror(errno);
        return false;
      }
      return true;
    }

    [[nodiscard]] bool validatePaletteObject(const nlohmann::json& palette, std::string& errorOut) {
      if (!palette.is_object()) {
        errorOut = "palette must be an object";
        return false;
      }
      for (const auto key : requiredPaletteKeys()) {
        const auto it = palette.find(std::string(key));
        if (it == palette.end() || !it->is_string()) {
          errorOut = std::string("palette is missing key '") + std::string(key) + "'";
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] bool installRegularFile(
        const std::filesystem::path& source, const std::filesystem::path& destination, mode_t mode,
        std::string& errorOut
    ) {
      std::error_code ec;
      std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        errorOut =
            std::string("failed to copy '") + source.string() + "' to '" + destination.string() + "': " + ec.message();
        return false;
      }
      return setMode(destination, mode, errorOut);
    }

    [[nodiscard]] bool removeInstalledWallpapers(const std::filesystem::path& syncedDir, std::string& errorOut) {
      std::error_code ec;
      if (!std::filesystem::is_directory(syncedDir, ec) || ec) {
        return true;
      }

      for (const auto& entry : std::filesystem::directory_iterator(syncedDir, ec)) {
        if (ec) {
          errorOut = std::string("failed to enumerate '") + syncedDir.string() + "': " + ec.message();
          return false;
        }
        const auto name = entry.path().filename().string();
        if (!isWallpaperFileName(name)) {
          continue;
        }
        std::filesystem::remove(entry.path(), ec);
        if (ec) {
          errorOut = std::string("failed to remove '") + entry.path().string() + "': " + ec.message();
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] std::vector<std::filesystem::path>
    stagingWallpaperFiles(const std::filesystem::path& stagingDirectory) {
      std::vector<std::filesystem::path> files;
      std::error_code ec;
      if (!std::filesystem::is_directory(stagingDirectory, ec) || ec) {
        return files;
      }

      for (const auto& entry : std::filesystem::directory_iterator(stagingDirectory, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
          continue;
        }
        const auto name = entry.path().filename().string();
        if (isWallpaperFileName(name)) {
          files.push_back(entry.path());
        }
      }
      return files;
    }

  } // namespace

  std::filesystem::path syncedDataDirectory() {
    const char* overrideDir = std::getenv(kSyncedDataDirEnv);
    if (overrideDir != nullptr && overrideDir[0] != '\0') {
      return std::filesystem::path(overrideDir);
    }
    return std::filesystem::path(kDefaultSyncedDataDir);
  }

  std::filesystem::path packageConfPath() { return syncedDataDirectory() / kGreeterTomlFileName; }

  std::filesystem::path manifestPath() { return syncedDataDirectory() / kManifestFileName; }

  bool syncedAppearanceInstalled() {
    std::error_code ec;
    return std::filesystem::is_regular_file(manifestPath(), ec) && !ec;
  }

  std::filesystem::path stagingManifestPath(const std::filesystem::path& stagingDirectory) {
    return stagingDirectory / kManifestFileName;
  }

  const std::vector<std::string_view>& requiredPaletteKeys() {
    static const std::vector<std::string_view> keys = {
        "primary", "on_primary", "secondary", "on_secondary", "tertiary",        "on_tertiary",
        "error",   "on_error",   "surface",   "on_surface",   "surface_variant", "on_surface_variant",
        "outline", "shadow",     "hover",     "on_hover",
    };
    return keys;
  }

  std::optional<WallpaperFillMode> parseFillMode(std::string_view value) {
    if (value == "center") {
      return WallpaperFillMode::Center;
    }
    if (value == "crop") {
      return WallpaperFillMode::Crop;
    }
    if (value == "fit") {
      return WallpaperFillMode::Fit;
    }
    if (value == "stretch") {
      return WallpaperFillMode::Stretch;
    }
    if (value == "repeat") {
      return WallpaperFillMode::Repeat;
    }
    return std::nullopt;
  }

  bool validateStagingManifest(const std::filesystem::path& stagingDirectory, std::string& errorOut) {
    const auto manifestFile = stagingManifestPath(stagingDirectory);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(manifestFile, ec) || ec) {
      errorOut = "missing appearance.json in staging directory";
      return false;
    }

    std::ifstream in(manifestFile);
    if (!in.is_open()) {
      errorOut = std::string("failed to open '") + manifestFile.string() + "'";
      return false;
    }

    try {
      const auto root = nlohmann::json::parse(in);
      if (!root.is_object()) {
        errorOut = "appearance.json root must be an object";
        return false;
      }

      const int version = root.value("version", 0);
      if (version != kManifestVersion) {
        errorOut = "unsupported appearance.json version";
        return false;
      }

      const auto paletteIt = root.find("palette");
      if (paletteIt == root.end()) {
        errorOut = "appearance.json is missing palette";
        return false;
      }
      if (!validatePaletteObject(*paletteIt, errorOut)) {
        return false;
      }

      const auto wallpaperIt = root.find("wallpaper");
      if (wallpaperIt != root.end() && !wallpaperIt->is_object()) {
        errorOut = "wallpaper must be an object when present";
        return false;
      }

      return true;
    } catch (const std::exception& e) {
      errorOut = std::string("failed to parse appearance.json: ") + e.what();
      return false;
    }
  }

  bool installFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut) {
    std::error_code ec;
    if (!std::filesystem::is_directory(stagingDirectory, ec) || ec) {
      errorOut = "staging directory does not exist";
      return false;
    }

    if (!validateStagingManifest(stagingDirectory, errorOut)) {
      return false;
    }

    const auto destination = syncedDataDirectory();
    std::filesystem::create_directories(destination, ec);
    if (ec) {
      errorOut = std::string("failed to create '") + destination.string() + "': " + ec.message();
      return false;
    }
    if (!setMode(destination, kSyncedDirMode, errorOut)) {
      return false;
    }

    if (!removeInstalledWallpapers(destination, errorOut)) {
      return false;
    }

    const auto manifestSource = stagingManifestPath(stagingDirectory);
    const auto manifestDestination = manifestPath();
    if (!installRegularFile(manifestSource, manifestDestination, kSyncedFileMode, errorOut)) {
      return false;
    }

    for (const auto& wallpaperSource : stagingWallpaperFiles(stagingDirectory)) {
      const auto wallpaperDestination = destination / wallpaperSource.filename();
      if (!installRegularFile(wallpaperSource, wallpaperDestination, kSyncedFileMode, errorOut)) {
        return false;
      }
    }

    return true;
  }

  bool applySyncedGreeterPreferences(const std::filesystem::path& stagingDirectory, std::string& errorOut) {
    std::optional<std::string> stagedOutputLayout;
    const auto layoutPath = stagingDirectory / kOutputLayoutFileName;
    std::error_code ec;
    if (std::filesystem::is_regular_file(layoutPath, ec) && !ec) {
      std::ifstream in(layoutPath);
      if (!in.is_open()) {
        errorOut = std::string("failed to open staged '") + layoutPath.string() + "'";
        return false;
      }
      std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) {
        raw.pop_back();
      }
      std::size_t begin = 0;
      while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
        ++begin;
      }
      std::size_t end = raw.size();
      while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1])) != 0) {
        --end;
      }
      const std::string trimmed = raw.substr(begin, end - begin);
      if (trimmed.empty()) {
        errorOut = "staged output_layout is empty";
        return false;
      }
      stagedOutputLayout = trimmed;
    }

    if (!greeter::applyAppearanceSyncGreeterConf(stagedOutputLayout)) {
      errorOut = "failed to update greeter.toml after appearance sync";
      return false;
    }
    return true;
  }

} // namespace greeter::appearance
