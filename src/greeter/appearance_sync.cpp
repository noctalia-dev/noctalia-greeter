#include "greeter/appearance_sync.h"

#include "core/log.h"
#include "greeter/greetd_user.h"
#include "greeter/greeter_preferences.h"
#include "greeter/privileged_state_paths.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace greeter::appearance {

  namespace {

    constexpr Logger kLog("greeter-appearance-sync");
    constexpr mode_t kSyncedDirMode = S_IRWXU | S_IRGRP | S_IXGRP;
    constexpr mode_t kSyncedFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    constexpr std::string_view kWallpaperTemporaryPrefix = ".wallpaper.tmp.";

    [[nodiscard]] bool isWallpaperFileName(std::string_view name) {
      // wallpaper, wallpaper.webp, wallpaper-DP-2.webp, wallpaper-HDMI-A-1.jpg
      if (name == kWallpaperBaseName) {
        return true;
      }
      constexpr std::string_view kDot = "wallpaper.";
      constexpr std::string_view kDash = "wallpaper-";
      return (name.size() > kDot.size() && name.substr(0, kDot.size()) == kDot)
          || (name.size() > kDash.size() && name.substr(0, kDash.size()) == kDash);
    }

    struct PendingWallpaperInstall {
      std::filesystem::path temporary;
      std::filesystem::path destination;
    };

    void cleanupPendingWallpapers(const std::vector<PendingWallpaperInstall>& pending) {
      for (const auto& install : pending) {
        if (!install.temporary.empty()) {
          ::unlink(install.temporary.c_str());
        }
      }
    }

    [[nodiscard]] bool isAbandonedWallpaperTemporary(std::string_view name) {
      if (!name.starts_with(kWallpaperTemporaryPrefix) || name.size() != kWallpaperTemporaryPrefix.size() + 6U) {
        return false;
      }
      for (const unsigned char ch : name.substr(kWallpaperTemporaryPrefix.size())) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
          return false;
        }
      }
      return true;
    }

    void cleanupAbandonedWallpaperTemporaries(const std::filesystem::path& directory) {
      std::error_code ec;
      std::filesystem::directory_iterator iterator(directory, ec);
      const std::filesystem::directory_iterator end;
      if (ec) {
        kLog.warn("failed to inspect abandoned wallpaper temporaries in '{}': {}", directory.string(), ec.message());
        return;
      }
      while (iterator != end) {
        const auto entry = *iterator;
        const std::string name = entry.path().filename().string();
        if (isAbandonedWallpaperTemporary(name)) {
          const auto status = entry.symlink_status(ec);
          if (ec) {
            kLog.warn("failed to inspect abandoned wallpaper temporary '{}': {}", entry.path().string(), ec.message());
            return;
          }
          if (std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
            std::filesystem::remove(entry.path(), ec);
            if (ec) {
              kLog.warn("failed to remove abandoned wallpaper temporary '{}': {}", entry.path().string(), ec.message());
              return;
            }
          }
        }
        iterator.increment(ec);
        if (ec) {
          kLog.warn("failed to enumerate wallpaper directory '{}': {}", directory.string(), ec.message());
          return;
        }
      }
    }

    [[nodiscard]] bool stageWallpaperFile(
        const std::filesystem::path& source, const std::filesystem::path& destination,
        PendingWallpaperInstall& installOut, std::string& errorOut
    ) {
      const int sourceFd = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
      if (sourceFd < 0) {
        errorOut = "failed to open staged wallpaper '" + source.string() + "': " + std::strerror(errno);
        return false;
      }
      struct stat sourceState{};
      if (::fstat(sourceFd, &sourceState) != 0) {
        const int savedErrno = errno;
        ::close(sourceFd);
        errorOut = "failed to inspect staged wallpaper '" + source.string() + "': " + std::strerror(savedErrno);
        return false;
      }
      if (!S_ISREG(sourceState.st_mode)) {
        ::close(sourceFd);
        errorOut = "staged wallpaper is not a regular file: '" + source.string() + "'";
        return false;
      }

      std::string temporary = (destination.parent_path() / ".wallpaper.tmp.XXXXXX").string();
      const int destinationFd = ::mkstemp(temporary.data());
      if (destinationFd < 0) {
        const int savedErrno = errno;
        ::close(sourceFd);
        errno = savedErrno;
        errorOut = "failed to create a temporary wallpaper for '" + destination.string() + "': " + std::strerror(errno);
        return false;
      }

      bool success = true;
      std::array<char, 64U * 1024U> buffer{};
      while (success) {
        const ssize_t count = ::read(sourceFd, buffer.data(), buffer.size());
        if (count < 0) {
          if (errno == EINTR) {
            continue;
          }
          errorOut = "failed to read staged wallpaper '" + source.string() + "': " + std::strerror(errno);
          success = false;
          break;
        }
        if (count == 0) {
          break;
        }

        std::size_t written = 0;
        while (written < static_cast<std::size_t>(count)) {
          const ssize_t result =
              ::write(destinationFd, buffer.data() + written, static_cast<std::size_t>(count) - written);
          if (result < 0) {
            if (errno == EINTR) {
              continue;
            }
            errorOut = "failed to stage wallpaper '" + destination.string() + "': " + std::strerror(errno);
            success = false;
            break;
          }
          if (result == 0) {
            errorOut = "failed to stage wallpaper '" + destination.string() + "': write returned zero";
            success = false;
            break;
          }
          written += static_cast<std::size_t>(result);
        }
      }

      if (success && ::fchmod(destinationFd, kSyncedFileMode) != 0) {
        errorOut = "failed to set mode on temporary wallpaper '" + temporary + "': " + std::strerror(errno);
        success = false;
      }
      if (success && ::fsync(destinationFd) != 0) {
        errorOut = "failed to flush temporary wallpaper '" + temporary + "': " + std::strerror(errno);
        success = false;
      }
      ::close(sourceFd);
      if (::close(destinationFd) != 0 && success) {
        errorOut = "failed to close temporary wallpaper '" + temporary + "': " + std::strerror(errno);
        success = false;
      }
      if (!success) {
        ::unlink(temporary.c_str());
        return false;
      }

      installOut = PendingWallpaperInstall{temporary, destination};
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

    [[nodiscard]] bool removeInstalledWallpapers(
        const std::filesystem::path& syncedDir, const std::unordered_set<std::string>& retained, std::string& errorOut
    ) {
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
        if (!isWallpaperFileName(name) || retained.contains(name)) {
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

    [[nodiscard]] bool stagingWallpaperFiles(
        const std::filesystem::path& stagingDirectory, std::vector<std::filesystem::path>& files, std::string& errorOut
    ) {
      files.clear();
      std::error_code ec;
      if (!std::filesystem::is_directory(stagingDirectory, ec) || ec) {
        errorOut = "failed to inspect staging directory '" + stagingDirectory.string() + "'";
        return false;
      }

      std::filesystem::directory_iterator iterator(stagingDirectory, ec);
      const std::filesystem::directory_iterator end;
      if (ec) {
        errorOut = "failed to enumerate staging directory '" + stagingDirectory.string() + "': " + ec.message();
        return false;
      }
      while (iterator != end) {
        const auto entry = *iterator;
        const bool regular = entry.is_regular_file(ec);
        if (ec) {
          errorOut = "failed to inspect staged entry '" + entry.path().string() + "': " + ec.message();
          return false;
        }
        if (regular) {
          const auto name = entry.path().filename().string();
          if (isWallpaperFileName(name)) {
            files.push_back(entry.path());
          }
        }
        iterator.increment(ec);
        if (ec) {
          errorOut = "failed to enumerate staging directory '" + stagingDirectory.string() + "': " + ec.message();
          return false;
        }
      }
      return true;
    }

    void cleanupStaleWallpapers(const std::filesystem::path& stagingDirectory) {
      std::vector<std::filesystem::path> wallpaperSources;
      std::string cleanupError;
      if (!stagingWallpaperFiles(stagingDirectory, wallpaperSources, cleanupError)) {
        kLog.warn("leaving stale wallpapers in place: {}", cleanupError);
        return;
      }

      std::unordered_set<std::string> retained;
      retained.reserve(wallpaperSources.size());
      for (const auto& source : wallpaperSources) {
        retained.emplace(source.filename().string());
      }
      if (!removeInstalledWallpapers(syncedDataDirectory(), retained, cleanupError)) {
        kLog.warn("leaving some stale wallpapers in place: {}", cleanupError);
      }
    }

    [[nodiscard]] std::optional<std::string>
    manifestOptionalString(const nlohmann::json& object, std::string_view key) {
      const auto it = object.find(std::string(key));
      if (it == object.end() || !it->is_string()) {
        return std::nullopt;
      }
      const std::string value = it->get<std::string>();
      return value.empty() ? std::nullopt : std::make_optional(value);
    }

    [[nodiscard]] config::GreeterTomlWallpaper parseManifestWallpaperObject(const nlohmann::json& wallpaper) {
      config::GreeterTomlWallpaper out;
      out.path = manifestOptionalString(wallpaper, "path");
      out.fillMode = manifestOptionalString(wallpaper, "fill_mode");
      out.fillColor = manifestOptionalString(wallpaper, "fill_color");
      return out;
    }

    [[nodiscard]] std::optional<config::GreeterSyncFile::SyncSessionAction>
    parseManifestSessionAction(const nlohmann::json& item) {
      if (!item.is_object()) {
        return std::nullopt;
      }
      const auto action = manifestOptionalString(item, "action");
      if (!action.has_value()) {
        return std::nullopt;
      }
      config::GreeterSyncFile::SyncSessionAction row;
      row.action = *action;
      row.command = manifestOptionalString(item, "command");
      row.label = manifestOptionalString(item, "label");
      row.glyph = manifestOptionalString(item, "glyph");
      return row;
    }

  } // namespace

  std::optional<ManifestSyncPayload> parseManifestForSync(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
      return std::nullopt;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
      kLog.warn("failed to open appearance manifest '{}'", path.string());
      return std::nullopt;
    }

    try {
      const auto root = nlohmann::json::parse(in);
      if (!root.is_object()) {
        return std::nullopt;
      }

      const int version = root.value("version", 0);
      if (version != kManifestVersion) {
        kLog.warn("unsupported appearance manifest version {} in '{}'", version, path.string());
        return std::nullopt;
      }

      ManifestSyncPayload payload;

      const auto paletteIt = root.find("palette");
      if (paletteIt == root.end() || !paletteIt->is_object()) {
        kLog.warn("appearance manifest '{}' is missing palette", path.string());
        return std::nullopt;
      }
      for (const auto& [key, value] : paletteIt->items()) {
        if (value.is_string()) {
          payload.appearance.palette[key] = value.get<std::string>();
        }
      }
      if (!payload.appearance.hasCompletePalette()) {
        kLog.warn("appearance manifest '{}' has incomplete palette", path.string());
        return std::nullopt;
      }

      payload.appearance.themeMode = manifestOptionalString(root, "theme_mode");
      if (const auto scaleIt = root.find("corner_radius_scale"); scaleIt != root.end() && scaleIt->is_number()) {
        payload.appearance.cornerRadiusScale = scaleIt->get<float>();
      }
      payload.appearance.fontFamily = manifestOptionalString(root, "font_family");

      if (const auto wallpaperIt = root.find("wallpaper"); wallpaperIt != root.end() && wallpaperIt->is_object()) {
        payload.appearance.wallpaper = parseManifestWallpaperObject(*wallpaperIt);
      }
      if (const auto wallpapersIt = root.find("wallpapers"); wallpapersIt != root.end() && wallpapersIt->is_object()) {
        for (const auto& [connector, value] : wallpapersIt->items()) {
          if (value.is_object() && !connector.empty()) {
            payload.appearance.wallpapers[connector] = parseManifestWallpaperObject(value);
          }
        }
      }

      if (const auto sessionIt = root.find("session"); sessionIt != root.end() && sessionIt->is_object()) {
        if (const auto powerIt = sessionIt->find("power"); powerIt != sessionIt->end() && powerIt->is_object()) {
          payload.sessionPowerSuspend = manifestOptionalString(*powerIt, "suspend");
          payload.sessionPowerReboot = manifestOptionalString(*powerIt, "reboot");
          payload.sessionPowerShutdown = manifestOptionalString(*powerIt, "shutdown");
        }
        if (const auto actionsIt = sessionIt->find("actions"); actionsIt != sessionIt->end() && actionsIt->is_array()) {
          for (const auto& item : *actionsIt) {
            if (auto row = parseManifestSessionAction(item)) {
              payload.sessionActions.push_back(std::move(*row));
            }
          }
        }
      }

      return payload;
    } catch (const std::exception& e) {
      kLog.warn("failed to parse appearance manifest '{}': {}", path.string(), e.what());
      return std::nullopt;
    }
  }

  std::filesystem::path syncedDataDirectory() {
    const char* overrideDir = std::getenv(kSyncedDataDirEnv);
    if (overrideDir != nullptr && overrideDir[0] != '\0') {
      return std::filesystem::path(overrideDir);
    }
    return std::filesystem::path(kDefaultSyncedDataDir);
  }

  std::filesystem::path packageConfPath() { return syncedDataDirectory() / kGreeterTomlFileName; }

  std::filesystem::path syncConfPath() { return syncedDataDirectory() / kSyncTomlFileName; }

  std::filesystem::path manifestPath() { return syncedDataDirectory() / kManifestFileName; }

  bool syncedAppearanceInstalled() {
    const config::GreeterSyncFile sync = config::loadSync(syncConfPath());
    if (sync.appearance.hasCompletePalette()) {
      return true;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(manifestPath(), ec) && !ec;
  }

  std::filesystem::path stagingSyncTomlPath(const std::filesystem::path& stagingDirectory) {
    return stagingDirectory / kSyncTomlFileName;
  }

  std::filesystem::path stagingManifestPath(const std::filesystem::path& stagingDirectory) {
    return stagingDirectory / kManifestFileName;
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
    std::error_code ec;
    const auto stagedSync = stagingSyncTomlPath(stagingDirectory);
    if (std::filesystem::is_regular_file(stagedSync, ec) && !ec) {
      const config::GreeterSyncFile sync = config::loadSync(stagedSync);
      if (!sync.appearance.hasCompletePalette()) {
        errorOut = "staged sync.toml is missing a complete appearance.palette";
        return false;
      }
      return true;
    }

    const auto manifestFile = stagingManifestPath(stagingDirectory);
    if (!std::filesystem::is_regular_file(manifestFile, ec) || ec) {
      errorOut = "missing staged sync.toml (or legacy appearance.json) in staging directory";
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
    if (!greeter::privileged_state::setMode(destination, kSyncedDirMode, errorOut)) {
      return false;
    }
    cleanupAbandonedWallpaperTemporaries(destination);

    std::vector<std::filesystem::path> wallpaperSources;
    if (!stagingWallpaperFiles(stagingDirectory, wallpaperSources, errorOut)) {
      return false;
    }
    std::vector<PendingWallpaperInstall> pending;
    pending.reserve(wallpaperSources.size());
    for (const auto& wallpaperSource : wallpaperSources) {
      const auto wallpaperDestination = destination / wallpaperSource.filename();
      PendingWallpaperInstall install;
      if (!stageWallpaperFile(wallpaperSource, wallpaperDestination, install, errorOut)) {
        cleanupPendingWallpapers(pending);
        return false;
      }
      pending.push_back(std::move(install));
    }

    for (auto& install : pending) {
      if (::rename(install.temporary.c_str(), install.destination.c_str()) != 0) {
        errorOut = "failed to install wallpaper '" + install.destination.string() + "': " + std::strerror(errno);
        cleanupPendingWallpapers(pending);
        return false;
      }
      install.temporary.clear();
    }

    const int destinationFd = ::open(destination.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (destinationFd >= 0) {
      if (::fsync(destinationFd) != 0) {
        kLog.warn("failed to flush wallpaper directory '{}': {}", destination.string(), std::strerror(errno));
      }
      ::close(destinationFd);
    }

    return true;
  }

  bool applySyncedGreeterPreferences(
      const std::filesystem::path& stagingDirectory, const bool includeSessionCommands, std::string& errorOut
  ) {
    auto readStagedText = [&](std::string_view fileName, std::optional<std::string>& out) -> bool {
      const auto path = stagingDirectory / fileName;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return true;
      }
      std::ifstream in(path);
      if (!in.is_open()) {
        errorOut = std::string("failed to open staged '") + path.string() + "'";
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
        errorOut = std::string("staged ") + std::string(fileName) + " is empty";
        return false;
      }
      out = trimmed;
      return true;
    };

    std::optional<std::string> stagedOutputLayout;
    std::optional<std::string> stagedOutputTransforms;
    std::optional<std::string> stagedOutputScales;
    if (!readStagedText(kOutputLayoutFileName, stagedOutputLayout)) {
      return false;
    }
    if (!readStagedText(kOutputTransformsFileName, stagedOutputTransforms)) {
      return false;
    }
    if (!readStagedText(kOutputScalesFileName, stagedOutputScales)) {
      return false;
    }

    const auto stagedSyncPath = stagingSyncTomlPath(stagingDirectory);
    std::error_code ec;
    greeter::GreeterSyncAppearanceUpdate appearanceUpdate;
    appearanceUpdate.replaceSession = includeSessionCommands;
    if (std::filesystem::is_regular_file(stagedSyncPath, ec) && !ec) {
      const config::GreeterSyncFile staged = config::loadSync(stagedSyncPath);
      if (!staged.appearance.hasCompletePalette()) {
        errorOut = "staged sync.toml is missing a complete appearance.palette";
        return false;
      }
      appearanceUpdate.appearance = staged.appearance;
      if (includeSessionCommands) {
        appearanceUpdate.sessionPowerSuspend = staged.sessionPowerSuspend;
        appearanceUpdate.sessionPowerReboot = staged.sessionPowerReboot;
        appearanceUpdate.sessionPowerShutdown = staged.sessionPowerShutdown;
        appearanceUpdate.sessionActions = staged.sessionActions;
      }
    } else {
      const auto manifestFile = stagingManifestPath(stagingDirectory);
      auto manifestPayload = parseManifestForSync(manifestFile);
      if (!manifestPayload.has_value()) {
        errorOut = std::string("failed to parse staged '") + manifestFile.string() + "'";
        return false;
      }
      appearanceUpdate.appearance = std::move(manifestPayload->appearance);
      if (includeSessionCommands) {
        appearanceUpdate.sessionPowerSuspend = std::move(manifestPayload->sessionPowerSuspend);
        appearanceUpdate.sessionPowerReboot = std::move(manifestPayload->sessionPowerReboot);
        appearanceUpdate.sessionPowerShutdown = std::move(manifestPayload->sessionPowerShutdown);
        appearanceUpdate.sessionActions = std::move(manifestPayload->sessionActions);
      }
    }

    if (!greeter::applyAppearanceSyncGreeterConf(
            stagedOutputLayout, stagedOutputTransforms, stagedOutputScales, appearanceUpdate
        )) {
      errorOut = "failed to update sync.toml after appearance sync";
      return false;
    }

    cleanupStaleWallpapers(stagingDirectory);

    // appearance.json is legacy; remove a stale live copy only after sync.toml
    // successfully commits, so a failed merge leaves all prior state usable.
    const auto legacyManifest = manifestPath();
    std::string manifestCleanupError;
    if (!greeter::privileged_state::removeSymlinkIfPresent(legacyManifest, manifestCleanupError)) {
      kLog.warn("failed to clean legacy appearance manifest: {}", manifestCleanupError);
      return true;
    }
    std::error_code cleanupError;
    if (std::filesystem::is_regular_file(legacyManifest, cleanupError) && !cleanupError) {
      std::filesystem::remove(legacyManifest, cleanupError);
      if (cleanupError) {
        kLog.warn("failed to remove legacy '{}': {}", legacyManifest.string(), cleanupError.message());
      }
    }
    return true;
  }

  bool ensureSyncedDataOwnedByGreeter(std::string& errorOut) {
    if (::geteuid() != 0) {
      return true;
    }

    const auto destination = syncedDataDirectory();
    const auto owner = greeter::resolveDataDirOwnership(destination, errorOut);
    if (!owner.has_value()) {
      return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(destination, ec) && !ec) {
      if (!greeter::privileged_state::setOwnership(destination, owner->uid, owner->gid, errorOut)) {
        return false;
      }
    }

    const auto syncPath = syncConfPath();
    if (std::filesystem::is_regular_file(syncPath, ec) && !ec) {
      if (!greeter::privileged_state::setOwnership(syncPath, owner->uid, owner->gid, errorOut)) {
        return false;
      }
    }

    if (!std::filesystem::is_directory(destination, ec) || ec) {
      return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(destination, ec)) {
      if (ec) {
        errorOut = std::string("failed to enumerate '") + destination.string() + "': " + ec.message();
        return false;
      }
      if (!entry.is_regular_file(ec) || ec) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (!isWallpaperFileName(name)) {
        continue;
      }
      if (!greeter::privileged_state::setOwnership(entry.path(), owner->uid, owner->gid, errorOut)) {
        return false;
      }
    }

    return true;
  }

} // namespace greeter::appearance
