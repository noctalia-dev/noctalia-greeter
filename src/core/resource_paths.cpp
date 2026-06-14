#include "core/resource_paths.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace paths {

  namespace {

    constexpr Logger kLog("paths");

    std::optional<std::filesystem::path> executablePath() {
      std::array<char, 4096> buffer{};
      const ssize_t count = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
      if (count <= 0 || static_cast<std::size_t>(count) >= buffer.size() - 1) {
        return std::nullopt;
      }

      buffer[static_cast<std::size_t>(count)] = '\0';
      return std::filesystem::path(buffer.data());
    }

    std::filesystem::path installedAssetsRoot() {
      std::filesystem::path installed(NOCTALIA_GREETER_INSTALLED_ASSETS_DIR);
      if (installed.is_absolute()) {
        return installed;
      }
      if (auto exe = executablePath()) {
        return exe->parent_path().parent_path() / installed;
      }
      return installed;
    }

    std::filesystem::path sourceAssetsRoot() { return std::filesystem::path(NOCTALIA_GREETER_ASSETS_DIR); }

    bool isAssetRoot(const std::filesystem::path& root) {
      if (root.empty()) {
        return false;
      }

      std::error_code ec;
      return std::filesystem::exists(root / "fonts" / "tabler.ttf", ec)
          && std::filesystem::exists(root / "fonts" / "tabler.json", ec);
    }

    void appendUnique(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& candidate) {
      if (candidate.empty()) {
        return;
      }
      if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
        candidates.push_back(candidate);
      }
    }

    std::vector<std::filesystem::path> assetCandidates() {
      std::vector<std::filesystem::path> candidates;

      if (const char* env = std::getenv("NOCTALIA_GREETER_ASSETS_DIR"); env != nullptr && env[0] != '\0') {
        const std::filesystem::path overridePath(env);
        if (isAssetRoot(overridePath)) {
          candidates.push_back(overridePath);
          return candidates;
        }
        kLog.warn("NOCTALIA_GREETER_ASSETS_DIR is not a valid asset bundle: {}", overridePath.string());
      }

      if (auto exe = executablePath()) {
        const std::filesystem::path exeDir = exe->parent_path();
        appendUnique(candidates, exeDir / "assets");
        appendUnique(candidates, exeDir.parent_path() / "assets");
        appendUnique(candidates, exeDir.parent_path() / "share" / "noctalia-greeter" / "assets");
      }

      appendUnique(candidates, installedAssetsRoot());
      appendUnique(candidates, sourceAssetsRoot());
      return candidates;
    }

    std::filesystem::path resolveAssetsRoot() {
      for (const auto& candidate : assetCandidates()) {
        if (isAssetRoot(candidate)) {
          kLog.debug("using assets from {}", candidate.string());
          return candidate;
        }
      }

      const std::filesystem::path fallback = sourceAssetsRoot();
      kLog.warn("could not locate a valid asset bundle; defaulting to {}", fallback.string());
      return fallback;
    }

  } // namespace

  const std::filesystem::path& assetsRoot() {
    static const std::filesystem::path s_root = resolveAssetsRoot();
    return s_root;
  }

  std::filesystem::path assetPath(std::string_view relativePath) {
    return assetsRoot() / std::filesystem::path(std::string(relativePath));
  }

} // namespace paths
