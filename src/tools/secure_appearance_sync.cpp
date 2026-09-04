#include "tools/secure_appearance_sync.h"

#include "greeter/appearance_sync.h"
#include "greeter/greetd_user.h"
#include "greeter/greeter_config_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <limits>
#include <linux/magic.h>
#include <optional>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace greeter::secure_sync {

  namespace {

    constexpr std::string_view kStagingDirectoryName = "noctalia-greeter-sync";
    constexpr std::string_view kSyncLockPath = "/run/noctalia-greeter-sync.lock";
    constexpr std::uint64_t kConfigSizeLimit = 256U * 1024U;
    // greeter_compositor_config stores each serialized output value in a 2048-byte buffer.
    constexpr std::uint64_t kOutputMetadataSizeLimit = 2047U;
    constexpr std::uint64_t kWallpaperSizeLimit = 64U * 1024U * 1024U;
    constexpr std::uint64_t kTotalSizeLimit = 128U * 1024U * 1024U;
    // Config + three output files + a fallback and up to sixteen per-output wallpapers.
    constexpr std::size_t kFileCountLimit = 22;
    constexpr std::size_t kOutputEntryLimit = 16;
    constexpr std::size_t kOutputNameLengthLimit = 127;
    constexpr std::size_t kPaletteEntryLimit = 64;
    constexpr std::size_t kPaletteKeyLengthLimit = 63;
    constexpr std::size_t kFontFamilyLengthLimit = 256;
    constexpr std::size_t kPasswdBufferSizeLimit = 1024U * 1024U;
    constexpr std::int64_t kOutputCoordinateLimit = 1'000'000;
    constexpr float kOutputScaleMinimum = 1.0F;
    constexpr float kOutputScaleMaximum = 10.0F;
    constexpr float kCornerRadiusScaleMinimum = 0.0F;
    constexpr float kCornerRadiusScaleMaximum = 2.0F;

    class UniqueFd {
    public:
      UniqueFd() = default;
      explicit UniqueFd(const int fd) : m_fd(fd) {}
      ~UniqueFd() {
        if (m_fd >= 0) {
          ::close(m_fd);
        }
      }

      UniqueFd(const UniqueFd&) = delete;
      UniqueFd& operator=(const UniqueFd&) = delete;

      UniqueFd(UniqueFd&& other) noexcept : m_fd(other.release()) {}
      UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
          if (m_fd >= 0) {
            ::close(m_fd);
          }
          m_fd = other.release();
        }
        return *this;
      }

      [[nodiscard]] int get() const { return m_fd; }
      [[nodiscard]] bool valid() const { return m_fd >= 0; }

    private:
      [[nodiscard]] int release() {
        const int fd = m_fd;
        m_fd = -1;
        return fd;
      }

      int m_fd = -1;
    };

    class TemporaryDirectory {
    public:
      ~TemporaryDirectory() {
        if (!m_path.empty()) {
          std::error_code ec;
          std::filesystem::remove_all(m_path, ec);
        }
      }

      TemporaryDirectory(const TemporaryDirectory&) = delete;
      TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
      TemporaryDirectory() = default;

      [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
      [[nodiscard]] int fd() const { return m_fd.get(); }

      [[nodiscard]] bool create(std::string& errorOut) {
        std::string pathTemplate = "/tmp/noctalia-greeter-sync.XXXXXX";
        char* created = ::mkdtemp(pathTemplate.data());
        if (created == nullptr) {
          errorOut = std::string("failed to create private staging directory: ") + std::strerror(errno);
          return false;
        }
        m_path = created;
        m_fd = UniqueFd(::open(m_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!m_fd.valid()) {
          errorOut = std::string("failed to open private staging directory: ") + std::strerror(errno);
          return false;
        }
        return true;
      }

    private:
      std::filesystem::path m_path;
      UniqueFd m_fd;
    };

    struct TargetAccount {
      uid_t uid = 0;
      gid_t gid = 0;
    };

    struct StagedFileSnapshot {
      std::string name;
      std::vector<char> contents;
    };

    enum class WritableEntryPolicy : std::uint8_t {
      Reject,
      AllowGroupWritableInPrivateRuntime,
    };

    [[nodiscard]] bool isAsciiAlphaNumeric(const unsigned char ch) {
      return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    }

    [[nodiscard]] bool isAsciiWhitespace(const unsigned char ch) {
      return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
    }

    [[nodiscard]] bool setErrnoError(std::string_view operation, std::string& errorOut) {
      errorOut = std::string(operation) + ": " + std::strerror(errno);
      return false;
    }

    [[nodiscard]] bool parseCallingUid(uid_t& uidOut, std::string& errorOut) {
      const char* raw = std::getenv("PKEXEC_UID");
      if (raw == nullptr || raw[0] == '\0') {
        errorOut = "--sync must be invoked through pkexec";
        return false;
      }

      std::uintmax_t parsed = 0;
      const char* end = raw + std::strlen(raw);
      const auto [ptr, ec] = std::from_chars(raw, end, parsed);
      if (ec != std::errc{} || ptr != end || parsed > std::numeric_limits<uid_t>::max()) {
        errorOut = "invalid PKEXEC_UID";
        return false;
      }
      uidOut = static_cast<uid_t>(parsed);
      return true;
    }

    [[nodiscard]] bool acquireSyncLock(UniqueFd& lockOut, std::string& errorOut) {
      UniqueFd lock(::open(kSyncLockPath.data(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR));
      if (!lock.valid()) {
        return setErrnoError("failed to open the appearance sync lock", errorOut);
      }

      struct stat state{};
      if (::fstat(lock.get(), &state) != 0) {
        return setErrnoError("failed to inspect the appearance sync lock", errorOut);
      }
      if (!S_ISREG(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "appearance sync lock has unsafe type, ownership, or permissions";
        return false;
      }
      if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          errorOut = "another greeter appearance sync is already in progress";
          return false;
        }
        return setErrnoError("failed to lock greeter appearance sync", errorOut);
      }

      lockOut = std::move(lock);
      return true;
    }

    [[nodiscard]] bool resolveTargetAccount(
        const std::filesystem::path& stateDirectory, TargetAccount& accountOut, std::string& errorOut
    ) {
      UniqueFd stateDir(::open(stateDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (!stateDir.valid()) {
        return setErrnoError("failed to open the greeter state directory", errorOut);
      }

      struct stat state{};
      if (::fstat(stateDir.get(), &state) != 0) {
        return setErrnoError("failed to inspect the greeter state directory", errorOut);
      }
      if (!S_ISDIR(state.st_mode) || state.st_uid == 0 || state.st_uid == static_cast<uid_t>(-1)) {
        errorOut = "greeter state directory must be owned by the non-root greeter account";
        return false;
      }
      if ((state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "greeter state directory must not be group- or world-writable";
        return false;
      }

      long suggestedSize = ::sysconf(_SC_GETPW_R_SIZE_MAX);
      if (suggestedSize < 1024 || suggestedSize > static_cast<long>(kPasswdBufferSizeLimit)) {
        suggestedSize = 16U * 1024U;
      }
      std::vector<char> buffer(static_cast<std::size_t>(suggestedSize));
      struct passwd record{};
      struct passwd* result = nullptr;
      int lookupError = 0;
      for (;;) {
        lookupError = ::getpwuid_r(state.st_uid, &record, buffer.data(), buffer.size(), &result);
        if (lookupError != ERANGE) {
          break;
        }
        if (buffer.size() >= kPasswdBufferSizeLimit) {
          errorOut = "greeter account record is unexpectedly large";
          return false;
        }
        buffer.resize(std::min(kPasswdBufferSizeLimit, buffer.size() * 2U));
      }
      if (lookupError != 0) {
        errorOut = std::string("failed to resolve the greeter account: ") + std::strerror(lookupError);
        return false;
      }
      if (result == nullptr) {
        errorOut = "greeter state directory owner has no account record";
        return false;
      }
      if (record.pw_uid != state.st_uid || record.pw_gid == 0 || record.pw_gid == static_cast<gid_t>(-1)) {
        errorOut = "greeter account must have a non-root primary group";
        return false;
      }

      accountOut = TargetAccount{state.st_uid, record.pw_gid};
      return true;
    }

    [[nodiscard]] bool
    parseOptionalUidEnvironment(const char* name, std::optional<uid_t>& uidOut, std::string& errorOut) {
      const char* raw = std::getenv(name);
      if (raw == nullptr || raw[0] == '\0') {
        return true;
      }
      std::uintmax_t parsed = 0;
      const char* end = raw + std::strlen(raw);
      const auto [ptr, ec] = std::from_chars(raw, end, parsed);
      if (ec != std::errc{} || ptr != end || parsed > std::numeric_limits<uid_t>::max()) {
        errorOut = std::string("invalid ") + name;
        return false;
      }
      uidOut = static_cast<uid_t>(parsed);
      return true;
    }

    [[nodiscard]] bool resolveLegacyCallerUid(std::optional<uid_t>& uidOut, std::string& errorOut) {
      std::optional<uid_t> pkexecUid;
      std::optional<uid_t> sudoUid;
      if (!parseOptionalUidEnvironment("PKEXEC_UID", pkexecUid, errorOut)
          || !parseOptionalUidEnvironment("SUDO_UID", sudoUid, errorOut)) {
        return false;
      }
      if (pkexecUid.has_value() && sudoUid.has_value() && *pkexecUid != *sudoUid) {
        errorOut = "PKEXEC_UID and SUDO_UID identify different callers";
        return false;
      }
      uidOut = pkexecUid.has_value() ? pkexecUid : sudoUid;
      return true;
    }

    [[nodiscard]] std::optional<uid_t> privateRuntimeStagingUid(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath
    ) {
      if (requestedPath.filename() != kStagingDirectoryName
          || requestedPath.parent_path().parent_path() != runtimeParentPath) {
        return std::nullopt;
      }

      const std::string uidComponent = requestedPath.parent_path().filename().string();
      std::uintmax_t parsed = 0;
      const char* begin = uidComponent.data();
      const char* end = begin + uidComponent.size();
      const auto [ptr, ec] = std::from_chars(begin, end, parsed);
      if (uidComponent.empty() || ec != std::errc{} || ptr != end || parsed > std::numeric_limits<uid_t>::max()) {
        return std::nullopt;
      }

      const auto uid = static_cast<uid_t>(parsed);
      if (uidComponent != std::to_string(uid)) {
        return std::nullopt;
      }
      return uid;
    }

    [[nodiscard]] bool openPrivateRuntimeStaging(
        const std::filesystem::path& runtimeParentPath, const uid_t runtimeParentOwner,
        const std::filesystem::path& requestedPath, const uid_t callerUid,
        const WritableEntryPolicy writableEntryPolicy, UniqueFd& stagingOut, std::string& errorOut
    ) {
      const std::filesystem::path runtimePath = runtimeParentPath / std::to_string(callerUid);
      const std::filesystem::path expectedPath = runtimePath / kStagingDirectoryName;
      if (requestedPath != expectedPath) {
        errorOut = "sync staging directory must be '" + expectedPath.string() + "'";
        return false;
      }

      UniqueFd runtimeParent(::open(runtimeParentPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (!runtimeParent.valid()) {
        return setErrnoError("failed to open the runtime directory parent", errorOut);
      }
      struct stat runtimeParentState{};
      if (::fstat(runtimeParent.get(), &runtimeParentState) != 0) {
        return setErrnoError("failed to inspect the runtime directory parent", errorOut);
      }
      if (!S_ISDIR(runtimeParentState.st_mode)
          || runtimeParentState.st_uid != runtimeParentOwner
          || (runtimeParentState.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "runtime directory parent has unsafe type, ownership, or permissions";
        return false;
      }

      const std::string runtimeName = std::to_string(callerUid);
      UniqueFd runtimeDir(
          ::openat(runtimeParent.get(), runtimeName.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
      );
      if (!runtimeDir.valid()) {
        return setErrnoError("failed to open the caller runtime directory", errorOut);
      }

      struct stat runtime{};
      if (::fstat(runtimeDir.get(), &runtime) != 0) {
        return setErrnoError("failed to inspect the caller runtime directory", errorOut);
      }
      if (!S_ISDIR(runtime.st_mode) || runtime.st_uid != callerUid || (runtime.st_mode & 0777) != 0700) {
        errorOut = "caller runtime directory has unsafe ownership or permissions";
        return false;
      }
      struct statfs runtimeFilesystem{};
      if (::fstatfs(runtimeDir.get(), &runtimeFilesystem) != 0) {
        return setErrnoError("failed to inspect the caller runtime filesystem", errorOut);
      }
      if (runtimeFilesystem.f_type == FUSE_SUPER_MAGIC) {
        errorOut = "caller runtime directory must not reside on a FUSE filesystem";
        return false;
      }

      UniqueFd staging(
          ::openat(runtimeDir.get(), kStagingDirectoryName.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
      );
      if (!staging.valid()) {
        return setErrnoError("failed to open the sync staging directory", errorOut);
      }

      struct stat state{};
      if (::fstat(staging.get(), &state) != 0) {
        return setErrnoError("failed to inspect the sync staging directory", errorOut);
      }
      const bool groupWritable = (state.st_mode & S_IWGRP) != 0;
      const bool allowGroupWritable = writableEntryPolicy == WritableEntryPolicy::AllowGroupWritableInPrivateRuntime;
      if (!S_ISDIR(state.st_mode)
          || state.st_uid != callerUid
          || (state.st_mode & S_IWOTH) != 0
          || (groupWritable && !allowGroupWritable)) {
        errorOut = "sync staging directory has unsafe ownership or permissions";
        return false;
      }
      if (state.st_dev != runtime.st_dev) {
        errorOut = "sync staging directory must reside directly on the caller runtime filesystem";
        return false;
      }

      stagingOut = std::move(staging);
      return true;
    }

    [[nodiscard]] bool openCallerStaging(
        const std::filesystem::path& requestedPath, const uid_t callerUid, UniqueFd& stagingOut, std::string& errorOut
    ) {
      const std::filesystem::path expectedPath =
          std::filesystem::path("/run/user") / std::to_string(callerUid) / kStagingDirectoryName;
      if (requestedPath != expectedPath) {
        errorOut = "--sync staging directory must be '" + expectedPath.string() + "'";
        return false;
      }
      return openPrivateRuntimeStaging(
          "/run/user", 0, requestedPath, callerUid, WritableEntryPolicy::Reject, stagingOut, errorOut
      );
    }

    [[nodiscard]] bool openLegacyStagingWithContext(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath,
        const uid_t runtimeParentOwner, const std::optional<uid_t> invokingUid, UniqueFd& stagingOut, uid_t& ownerOut,
        WritableEntryPolicy& writableEntryPolicyOut, std::string& errorOut
    ) {
      if (!requestedPath.is_absolute()) {
        errorOut = "legacy sync staging directory must be an absolute path";
        return false;
      }

      if (const auto runtimeUid = privateRuntimeStagingUid(requestedPath, runtimeParentPath)) {
        if (invokingUid.has_value() && *runtimeUid != *invokingUid) {
          errorOut = "legacy sync staging directory is not owned by the invoking user";
          return false;
        }
        if (!openPrivateRuntimeStaging(
                runtimeParentPath, runtimeParentOwner, requestedPath, *runtimeUid,
                WritableEntryPolicy::AllowGroupWritableInPrivateRuntime, stagingOut, errorOut
            )) {
          return false;
        }
        ownerOut = *runtimeUid;
        writableEntryPolicyOut = WritableEntryPolicy::AllowGroupWritableInPrivateRuntime;
        return true;
      }

      UniqueFd staging(::open(requestedPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (!staging.valid()) {
        return setErrnoError("failed to open the legacy sync staging directory", errorOut);
      }
      struct stat state{};
      if (::fstat(staging.get(), &state) != 0) {
        return setErrnoError("failed to inspect the legacy sync staging directory", errorOut);
      }
      if (!S_ISDIR(state.st_mode)
          || state.st_uid == static_cast<uid_t>(-1)
          || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "legacy sync staging directory has unsafe type, ownership, or permissions";
        return false;
      }
      struct statfs filesystemState{};
      if (::fstatfs(staging.get(), &filesystemState) != 0) {
        return setErrnoError("failed to inspect the legacy sync staging filesystem", errorOut);
      }
      if (filesystemState.f_type == FUSE_SUPER_MAGIC) {
        errorOut = "legacy sync staging directory must not reside on a FUSE filesystem";
        return false;
      }

      if (invokingUid.has_value() && state.st_uid != *invokingUid) {
        errorOut = "legacy sync staging directory is not owned by the invoking user";
        return false;
      }

      ownerOut = state.st_uid;
      writableEntryPolicyOut = WritableEntryPolicy::Reject;
      stagingOut = std::move(staging);
      return true;
    }

    [[nodiscard]] bool openLegacyStaging(
        const std::filesystem::path& requestedPath, UniqueFd& stagingOut, uid_t& ownerOut,
        WritableEntryPolicy& writableEntryPolicyOut, std::string& errorOut
    ) {
      std::optional<uid_t> invokingUid;
      if (!resolveLegacyCallerUid(invokingUid, errorOut)) {
        return false;
      }
      return openLegacyStagingWithContext(
          requestedPath, "/run/user", 0, invokingUid, stagingOut, ownerOut, writableEntryPolicyOut, errorOut
      );
    }

    [[nodiscard]] bool isWallpaperFileName(std::string_view name) {
      if (name == appearance::kWallpaperBaseName) {
        return true;
      }
      constexpr std::string_view kDotPrefix = "wallpaper.";
      constexpr std::string_view kOutputPrefix = "wallpaper-";
      const bool hasPrefix = (name.size() > kDotPrefix.size() && name.starts_with(kDotPrefix))
          || (name.size() > kOutputPrefix.size() && name.starts_with(kOutputPrefix));
      if (!hasPrefix || name.size() > 255) {
        return false;
      }
      return std::ranges::all_of(name, [](const unsigned char ch) {
        return isAsciiAlphaNumeric(ch) || ch == '.' || ch == '-' || ch == '_';
      });
    }

    [[nodiscard]] std::optional<std::uint64_t> sizeLimitForFile(std::string_view name, const bool allowLegacyManifest) {
      if (name == appearance::kSyncTomlFileName) {
        return kConfigSizeLimit;
      }
      if (allowLegacyManifest && name == appearance::kManifestFileName) {
        return kConfigSizeLimit;
      }
      if (name == appearance::kOutputLayoutFileName
          || name == appearance::kOutputTransformsFileName
          || name == appearance::kOutputScalesFileName) {
        return kOutputMetadataSizeLimit;
      }
      if (isWallpaperFileName(name)) {
        return kWallpaperSizeLimit;
      }
      return std::nullopt;
    }

    [[nodiscard]] bool snapshotFileContents(
        const int sourceFd, const std::uint64_t fileLimit, std::uint64_t& totalBytes, std::vector<char>& contentsOut,
        std::string& errorOut
    ) {
      std::array<char, 64U * 1024U> buffer{};
      std::uint64_t fileBytes = 0;
      for (;;) {
        const ssize_t count = ::read(sourceFd, buffer.data(), buffer.size());
        if (count < 0) {
          if (errno == EINTR) {
            continue;
          }
          return setErrnoError("failed to read a staged file", errorOut);
        }
        if (count == 0) {
          return true;
        }

        const auto bytesRead = static_cast<std::uint64_t>(count);
        if (bytesRead > fileLimit - fileBytes || bytesRead > kTotalSizeLimit - totalBytes) {
          errorOut = "sync staging data exceeds the allowed size";
          return false;
        }
        fileBytes += bytesRead;
        totalBytes += bytesRead;
        contentsOut.insert(contentsOut.end(), buffer.data(), buffer.data() + count);
      }
    }

    [[nodiscard]] bool snapshotStagingFiles(
        const int sourceDirFd, const uid_t callerUid, std::vector<StagedFileSnapshot>& snapshots,
        std::unordered_set<std::string>& wallpaperFiles, const bool allowLegacyManifest,
        const WritableEntryPolicy writableEntryPolicy, std::string& errorOut
    ) {
      struct stat sourceDirectoryState{};
      if (::fstat(sourceDirFd, &sourceDirectoryState) != 0) {
        return setErrnoError("failed to inspect the sync staging directory", errorOut);
      }

      const int iterationFd = ::fcntl(sourceDirFd, F_DUPFD_CLOEXEC, 3);
      if (iterationFd < 0) {
        return setErrnoError("failed to duplicate the sync staging directory", errorOut);
      }
      DIR* rawDirectory = ::fdopendir(iterationFd);
      if (rawDirectory == nullptr) {
        ::close(iterationFd);
        return setErrnoError("failed to enumerate the sync staging directory", errorOut);
      }

      std::uint64_t totalBytes = 0;
      bool foundSyncToml = false;
      bool foundLegacyManifest = false;
      errno = 0;
      while (const dirent* entry = ::readdir(rawDirectory)) {
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
          continue;
        }
        if (snapshots.size() >= kFileCountLimit) {
          ::closedir(rawDirectory);
          errorOut = "sync staging directory contains too many files";
          return false;
        }

        const auto fileLimit = sizeLimitForFile(name, allowLegacyManifest);
        if (!fileLimit.has_value()) {
          ::closedir(rawDirectory);
          errorOut = "unexpected file in sync staging directory: '" + std::string(name) + "'";
          return false;
        }

        UniqueFd source(::openat(sourceDirFd, entry->d_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (!source.valid()) {
          const int savedErrno = errno;
          ::closedir(rawDirectory);
          errno = savedErrno;
          return setErrnoError("failed to open a staged file", errorOut);
        }

        struct stat state{};
        if (::fstat(source.get(), &state) != 0) {
          const int savedErrno = errno;
          ::closedir(rawDirectory);
          errno = savedErrno;
          return setErrnoError("failed to inspect a staged file", errorOut);
        }
        const bool groupWritable = (state.st_mode & S_IWGRP) != 0;
        const bool allowGroupWritable = writableEntryPolicy == WritableEntryPolicy::AllowGroupWritableInPrivateRuntime;
        if (!S_ISREG(state.st_mode)
            || state.st_uid != callerUid
            || (state.st_mode & S_IWOTH) != 0
            || (groupWritable && (!allowGroupWritable || state.st_nlink != 1))) {
          ::closedir(rawDirectory);
          errorOut = "staged file has unsafe type, ownership, or permissions: '" + std::string(name) + "'";
          return false;
        }
        if (state.st_dev != sourceDirectoryState.st_dev) {
          ::closedir(rawDirectory);
          errorOut = "staged files must reside on the caller runtime filesystem: '" + std::string(name) + "'";
          return false;
        }
        if (state.st_size < 0 || static_cast<std::uint64_t>(state.st_size) > *fileLimit) {
          ::closedir(rawDirectory);
          errorOut = "staged file exceeds the allowed size: '" + std::string(name) + "'";
          return false;
        }

        StagedFileSnapshot snapshot;
        snapshot.name = name;
        snapshot.contents.reserve(static_cast<std::size_t>(state.st_size));
        if (!snapshotFileContents(source.get(), *fileLimit, totalBytes, snapshot.contents, errorOut)) {
          ::closedir(rawDirectory);
          return false;
        }

        if (name == appearance::kSyncTomlFileName) {
          foundSyncToml = true;
        } else if (name == appearance::kManifestFileName) {
          foundLegacyManifest = true;
        } else if (isWallpaperFileName(name)) {
          wallpaperFiles.emplace(name);
        }
        snapshots.push_back(std::move(snapshot));
        errno = 0;
      }

      const int iterationError = errno;
      ::closedir(rawDirectory);
      if (iterationError != 0) {
        errno = iterationError;
        return setErrnoError("failed while enumerating the sync staging directory", errorOut);
      }
      if (!foundSyncToml && (!allowLegacyManifest || !foundLegacyManifest)) {
        errorOut = allowLegacyManifest ? "sync staging directory is missing sync.toml or appearance.json"
                                       : "sync staging directory is missing sync.toml";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool
    writeFileContents(const int destinationFd, const std::vector<char>& contents, std::string& errorOut) {
      std::size_t written = 0;
      while (written < contents.size()) {
        const ssize_t result = ::write(destinationFd, contents.data() + written, contents.size() - written);
        if (result < 0) {
          if (errno == EINTR) {
            continue;
          }
          return setErrnoError("failed to write a private staged file", errorOut);
        }
        if (result == 0) {
          errorOut = "failed to write a private staged file: write returned zero";
          return false;
        }
        written += static_cast<std::size_t>(result);
      }
      return true;
    }

    [[nodiscard]] bool materializeSnapshots(
        const TemporaryDirectory& destination, const std::vector<StagedFileSnapshot>& snapshots, std::string& errorOut
    ) {
      for (const auto& snapshot : snapshots) {
        UniqueFd target(
            ::openat(
                destination.fd(), snapshot.name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                S_IRUSR | S_IWUSR
            )
        );
        if (!target.valid()) {
          return setErrnoError("failed to create a private staged file", errorOut);
        }
        if (!writeFileContents(target.get(), snapshot.contents, errorOut)) {
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] bool isHexColor(std::string_view value) {
      if (value.size() != 7 && value.size() != 9) {
        return false;
      }
      if (value.front() != '#') {
        return false;
      }
      return std::ranges::all_of(value.substr(1), [](const unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
      });
    }

    [[nodiscard]] bool isSafePaletteKey(std::string_view key) {
      if (key.empty() || key.size() > kPaletteKeyLengthLimit) {
        return false;
      }
      return std::ranges::all_of(key, [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
      });
    }

    [[nodiscard]] bool isSafeFontFamily(std::string_view family) {
      return !family.empty()
          && family.size() <= kFontFamilyLengthLimit
          && std::ranges::none_of(family, [](const unsigned char ch) { return ch < 0x20U || ch == 0x7FU; });
    }

    enum class OutputMetadataKind : std::uint8_t {
      Layout,
      Transforms,
      Scales,
    };

    [[nodiscard]] bool isOutputSeparator(const char ch) { return ch == ' ' || ch == '\t' || ch == ';'; }

    [[nodiscard]] bool isSafeOutputName(std::string_view name) {
      if (name.empty() || name.size() > kOutputNameLengthLimit) {
        return false;
      }
      return std::ranges::all_of(name, [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '-'
            || ch == '_'
            || ch == '.';
      });
    }

    [[nodiscard]] bool parseBoundedCoordinate(std::string_view raw) {
      std::int64_t value = 0;
      const auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
      return ec == std::errc{}
      && end == raw.data() + raw.size()
          && value >= -kOutputCoordinateLimit
          && value <= kOutputCoordinateLimit;
    }

    [[nodiscard]] bool isValidTransform(std::string_view value) {
      return value == "normal"
          || value == "0"
          || value == "none"
          || value == "90"
          || value == "180"
          || value == "270"
          || value == "flipped"
          || value == "flipped-90"
          || value == "flipped_90"
          || value == "flipped-180"
          || value == "flipped_180"
          || value == "flipped-270"
          || value == "flipped_270";
    }

    [[nodiscard]] bool isValidScale(std::string_view value) {
      const std::string terminated(value);
      char* end = nullptr;
      errno = 0;
      const float parsed = std::strtof(terminated.c_str(), &end);
      return errno == 0
          && end != terminated.c_str()
          && end == terminated.c_str() + terminated.size()
          && std::isfinite(parsed)
          && parsed >= kOutputScaleMinimum
          && parsed <= kOutputScaleMaximum;
    }

    [[nodiscard]] bool validateOutputEntry(std::string_view token, const OutputMetadataKind kind) {
      const std::size_t colon = token.rfind(':');
      if (colon == std::string_view::npos || colon == 0 || colon + 1 >= token.size()) {
        return false;
      }
      const std::string_view name = token.substr(0, colon);
      const std::string_view value = token.substr(colon + 1);
      if (!isSafeOutputName(name)) {
        return false;
      }

      if (kind == OutputMetadataKind::Transforms) {
        return isValidTransform(value);
      }
      if (kind == OutputMetadataKind::Scales) {
        return isValidScale(value);
      }

      const std::size_t comma = value.find(',');
      if (comma == std::string_view::npos
          || comma == 0
          || comma + 1 >= value.size()
          || value.find(',', comma + 1) != std::string_view::npos) {
        return false;
      }
      return parseBoundedCoordinate(value.substr(0, comma)) && parseBoundedCoordinate(value.substr(comma + 1));
    }

    [[nodiscard]] bool validateOutputMetadata(
        const std::filesystem::path& staging, std::string_view fileName, const OutputMetadataKind kind,
        std::string& errorOut
    ) {
      const auto path = staging / fileName;
      std::error_code filesystemError;
      const auto status = std::filesystem::symlink_status(path, filesystemError);
      if (filesystemError == std::errc::no_such_file_or_directory) {
        return true;
      }
      if (filesystemError) {
        errorOut = "failed to inspect staged " + std::string(fileName) + ": " + filesystemError.message();
        return false;
      }
      if (status.type() == std::filesystem::file_type::not_found) {
        return true;
      }
      if (!std::filesystem::is_regular_file(status)) {
        errorOut = "staged " + std::string(fileName) + " is not a regular file";
        return false;
      }

      std::ifstream input(path, std::ios::binary);
      if (!input.is_open()) {
        errorOut = "failed to open staged " + std::string(fileName);
        return false;
      }
      std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      if (!input.good() && !input.eof()) {
        errorOut = "failed to read staged " + std::string(fileName);
        return false;
      }
      if (raw.find('\0') != std::string::npos) {
        errorOut = "staged " + std::string(fileName) + " contains a NUL byte";
        return false;
      }
      while (!raw.empty() && isAsciiWhitespace(static_cast<unsigned char>(raw.front()))) {
        raw.erase(raw.begin());
      }
      while (!raw.empty() && isAsciiWhitespace(static_cast<unsigned char>(raw.back()))) {
        raw.pop_back();
      }
      if (raw.empty() || raw.size() > kOutputMetadataSizeLimit) {
        errorOut = "staged " + std::string(fileName) + " is empty or too large";
        return false;
      }

      std::unordered_set<std::string> names;
      std::size_t entryCount = 0;
      std::size_t cursor = 0;
      while (cursor < raw.size()) {
        while (cursor < raw.size() && isOutputSeparator(raw[cursor])) {
          ++cursor;
        }
        if (cursor == raw.size()) {
          break;
        }
        if (isAsciiWhitespace(static_cast<unsigned char>(raw[cursor]))) {
          errorOut = "staged " + std::string(fileName) + " contains an unsupported whitespace separator";
          return false;
        }

        std::size_t end = cursor;
        while (end < raw.size() && !isOutputSeparator(raw[end])) {
          if (isAsciiWhitespace(static_cast<unsigned char>(raw[end]))) {
            errorOut = "staged " + std::string(fileName) + " contains an unsupported whitespace separator";
            return false;
          }
          ++end;
        }
        const std::string_view token(raw.data() + cursor, end - cursor);
        if (++entryCount > kOutputEntryLimit || !validateOutputEntry(token, kind)) {
          errorOut = "staged " + std::string(fileName) + " contains an invalid output entry";
          return false;
        }
        const std::string name(token.substr(0, token.rfind(':')));
        if (!names.emplace(name).second) {
          errorOut = "staged " + std::string(fileName) + " contains a duplicate output name";
          return false;
        }
        cursor = end;
      }
      if (entryCount == 0) {
        errorOut = "staged " + std::string(fileName) + " contains no output entries";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool validateWallpaper(
        const config::GreeterTomlWallpaper& wallpaper, const std::unordered_set<std::string>& stagedWallpapers,
        std::string& errorOut
    ) {
      if (wallpaper.fillMode.has_value() && !appearance::parseFillMode(*wallpaper.fillMode).has_value()) {
        errorOut = "staged wallpaper has an invalid fill mode";
        return false;
      }
      if (wallpaper.fillColor.has_value() && !isHexColor(*wallpaper.fillColor)) {
        errorOut = "staged wallpaper has an invalid fill color";
        return false;
      }
      if (!wallpaper.path.has_value() || wallpaper.path->empty()) {
        return true;
      }

      constexpr std::string_view kColorPrefix = "color:";
      if (wallpaper.path->starts_with(kColorPrefix)) {
        if (!isHexColor(std::string_view(*wallpaper.path).substr(kColorPrefix.size()))) {
          errorOut = "staged wallpaper has an invalid solid color";
          return false;
        }
        return true;
      }

      const std::filesystem::path path(*wallpaper.path);
      const std::string fileName = path.filename().string();
      if (path.parent_path() != std::filesystem::path(appearance::kDefaultSyncedDataDir)
          || !isWallpaperFileName(fileName)
          || !stagedWallpapers.contains(fileName)) {
        errorOut = "staged wallpaper path must reference an accompanying greeter wallpaper file";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool validatePayload(
        const TemporaryDirectory& staging, const std::unordered_set<std::string>& stagedWallpapers,
        std::string& errorOut
    ) {
      config::clearConfigDiagnostics();
      const config::GreeterSyncFile sync = config::loadSync(staging.path() / appearance::kSyncTomlFileName);
      if (!config::configDiagnostics().empty()) {
        errorOut = "staged sync.toml is invalid: " + config::configDiagnostics().front().message;
        return false;
      }
      if (!sync.appearance.hasCompletePalette()) {
        errorOut = "staged sync.toml is missing a complete appearance.palette";
        return false;
      }
      if (sync.appearanceScheme.has_value() && *sync.appearanceScheme != appearance::kSyncedSchemeDisplayName) {
        errorOut = "staged sync.toml appearance.scheme must be 'Synced'";
        return false;
      }
      if (sync.appearance.themeMode.has_value()
          && *sync.appearance.themeMode != "light"
          && *sync.appearance.themeMode != "dark") {
        errorOut = "staged sync.toml contains an invalid appearance.theme_mode";
        return false;
      }
      if (sync.appearance.cornerRadiusScale.has_value()
          && (!std::isfinite(*sync.appearance.cornerRadiusScale)
              || *sync.appearance.cornerRadiusScale < kCornerRadiusScaleMinimum
              || *sync.appearance.cornerRadiusScale > kCornerRadiusScaleMaximum)) {
        errorOut = "staged sync.toml contains an invalid appearance.corner_radius_scale";
        return false;
      }
      if (sync.appearance.fontFamily.has_value() && !isSafeFontFamily(*sync.appearance.fontFamily)) {
        errorOut = "staged sync.toml contains an invalid appearance.font_family";
        return false;
      }
      if (sync.appearance.palette.size() > kPaletteEntryLimit) {
        errorOut = "staged sync.toml appearance.palette contains too many entries";
        return false;
      }
      for (const auto& [key, value] : sync.appearance.palette) {
        if (!isSafePaletteKey(key) || !isHexColor(value)) {
          errorOut = "staged sync.toml contains an invalid appearance palette";
          return false;
        }
      }
      if (sync.sessionLast.has_value()
          || sync.sessionPowerSuspend.has_value()
          || sync.sessionPowerReboot.has_value()
          || sync.sessionPowerShutdown.has_value()
          || !sync.sessionActions.empty()) {
        errorOut = "passwordless appearance sync cannot change session configuration";
        return false;
      }
      if (sync.outputLayout.has_value() || sync.outputTransforms.has_value() || sync.outputScales.has_value()) {
        errorOut = "output metadata must use the dedicated staging files";
        return false;
      }
      if (sync.appearance.wallpaper.has_value()
          && !validateWallpaper(*sync.appearance.wallpaper, stagedWallpapers, errorOut)) {
        return false;
      }
      if (sync.appearance.wallpapers.size() > kOutputEntryLimit) {
        errorOut = "staged sync.toml appearance.wallpapers contains too many connector entries";
        return false;
      }
      for (const auto& [connector, wallpaper] : sync.appearance.wallpapers) {
        if (!isSafeOutputName(connector)) {
          errorOut = "staged sync.toml appearance.wallpapers contains an invalid connector name";
          return false;
        }
        if (!validateWallpaper(wallpaper, stagedWallpapers, errorOut)) {
          return false;
        }
      }
      if (!validateOutputMetadata(
              staging.path(), appearance::kOutputLayoutFileName, OutputMetadataKind::Layout, errorOut
          )) {
        return false;
      }
      if (!validateOutputMetadata(
              staging.path(), appearance::kOutputTransformsFileName, OutputMetadataKind::Transforms, errorOut
          )) {
        return false;
      }
      if (!validateOutputMetadata(
              staging.path(), appearance::kOutputScalesFileName, OutputMetadataKind::Scales, errorOut
          )) {
        return false;
      }
      return true;
    }

    [[nodiscard]] bool validateLiveSync(std::string& errorOut) {
      const std::filesystem::path path = appearance::syncConfPath();
      std::error_code filesystemError;
      const auto status = std::filesystem::status(path, filesystemError);
      if (filesystemError == std::errc::no_such_file_or_directory
          || status.type() == std::filesystem::file_type::not_found) {
        return true;
      }
      if (filesystemError) {
        errorOut = "failed to inspect live sync.toml: " + filesystemError.message();
        return false;
      }
      if (!std::filesystem::is_regular_file(status)) {
        errorOut = "live sync.toml is not a regular file";
        return false;
      }

      config::clearConfigDiagnostics();
      (void)config::loadSync(path);
      if (!config::configDiagnostics().empty()) {
        errorOut =
            "live sync.toml is invalid and will not be overwritten: " + config::configDiagnostics().front().message;
        return false;
      }
      return true;
    }

    [[nodiscard]] bool dropPrivileges(const TargetAccount& account, std::string& errorOut) {
      if (::setgroups(0, nullptr) != 0) {
        return setErrnoError("failed to clear supplementary groups", errorOut);
      }
      if (::setresgid(account.gid, account.gid, account.gid) != 0) {
        return setErrnoError("failed to drop greeter group privileges", errorOut);
      }
      if (::setresuid(account.uid, account.uid, account.uid) != 0) {
        return setErrnoError("failed to drop greeter user privileges", errorOut);
      }
      if (::getuid() != account.uid
          || ::geteuid() != account.uid
          || ::getgid() != account.gid
          || ::getegid() != account.gid) {
        errorOut = "failed to verify dropped greeter privileges";
        return false;
      }
      return true;
    }

  } // namespace

#ifdef NOCTALIA_GREETER_TESTING
  namespace detail {

    bool validateLegacyStagingForTesting(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath,
        const uid_t runtimeParentOwner, const std::optional<uid_t> invokingUid, std::string& errorOut
    ) {
      UniqueFd staging;
      uid_t stagingOwner = 0;
      WritableEntryPolicy writableEntryPolicy = WritableEntryPolicy::Reject;
      if (!openLegacyStagingWithContext(
              requestedPath, runtimeParentPath, runtimeParentOwner, invokingUid, staging, stagingOwner,
              writableEntryPolicy, errorOut
          )) {
        return false;
      }

      std::vector<StagedFileSnapshot> snapshots;
      std::unordered_set<std::string> wallpaperFiles;
      return snapshotStagingFiles(
          staging.get(), stagingOwner, snapshots, wallpaperFiles, /*allowLegacyManifest=*/true, writableEntryPolicy,
          errorOut
      );
    }

    bool validateConstrainedStagingForTesting(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath,
        const uid_t runtimeParentOwner, const uid_t callerUid, std::string& errorOut
    ) {
      UniqueFd staging;
      if (!openPrivateRuntimeStaging(
              runtimeParentPath, runtimeParentOwner, requestedPath, callerUid, WritableEntryPolicy::Reject, staging,
              errorOut
          )) {
        return false;
      }

      std::vector<StagedFileSnapshot> snapshots;
      std::unordered_set<std::string> wallpaperFiles;
      return snapshotStagingFiles(
          staging.get(), callerUid, snapshots, wallpaperFiles, /*allowLegacyManifest=*/false,
          WritableEntryPolicy::Reject, errorOut
      );
    }

  } // namespace detail
#endif

  bool applyFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut) {
    if (::geteuid() != 0) {
      errorOut = "--sync requires the privileged pkexec helper";
      return false;
    }

    // The generic logger supports an environment-selected file target. Do not let a
    // caller-controlled value become a root file open on an early validation error.
    ::unsetenv("NOCTALIA_GREETER_LOG");

    uid_t callerUid = 0;
    if (!parseCallingUid(callerUid, errorOut)) {
      return false;
    }

    ::unsetenv(appearance::kSyncedDataDirEnv);
    ::unsetenv(kGreeterUserEnv);
    ::unsetenv("GREETD_CONFIG");

    UniqueFd syncLock;
    if (!acquireSyncLock(syncLock, errorOut)) {
      return false;
    }

    TargetAccount target;
    if (!resolveTargetAccount(appearance::kDefaultSyncedDataDir, target, errorOut)) {
      return false;
    }

    UniqueFd callerStaging;
    if (!openCallerStaging(stagingDirectory, callerUid, callerStaging, errorOut)) {
      return false;
    }

    // Root only snapshots bounded bytes from caller-owned, no-follow files. Parsing and every
    // mutation happen after the irreversible privilege drop below.
    std::vector<StagedFileSnapshot> snapshots;
    std::unordered_set<std::string> wallpaperFiles;
    if (!snapshotStagingFiles(
            callerStaging.get(), callerUid, snapshots, wallpaperFiles, /*allowLegacyManifest=*/false,
            WritableEntryPolicy::Reject, errorOut
        )) {
      return false;
    }
    callerStaging = UniqueFd{};
    if (!dropPrivileges(target, errorOut)) {
      return false;
    }

    TemporaryDirectory privateStaging;
    if (!privateStaging.create(errorOut)) {
      return false;
    }
    if (!materializeSnapshots(privateStaging, snapshots, errorOut)) {
      return false;
    }
    if (!validatePayload(privateStaging, wallpaperFiles, errorOut)) {
      return false;
    }
    if (!validateLiveSync(errorOut)) {
      return false;
    }
    if (!appearance::installFromStaging(privateStaging.path(), errorOut)) {
      return false;
    }
    return appearance::applySyncedGreeterPreferences(privateStaging.path(), /*includeSessionCommands=*/false, errorOut);
  }

  bool applyLegacyFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut) {
    if (::geteuid() != 0) {
      errorOut = "legacy sync requires an administrator privilege helper";
      return false;
    }

    ::unsetenv("NOCTALIA_GREETER_LOG");

    UniqueFd syncLock;
    if (!acquireSyncLock(syncLock, errorOut)) {
      return false;
    }

    TargetAccount target;
    if (!resolveTargetAccount(appearance::syncedDataDirectory(), target, errorOut)) {
      return false;
    }

    UniqueFd callerStaging;
    uid_t callerUid = 0;
    WritableEntryPolicy writableEntryPolicy = WritableEntryPolicy::Reject;
    if (!openLegacyStaging(stagingDirectory, callerStaging, callerUid, writableEntryPolicy, errorOut)) {
      return false;
    }

    std::vector<StagedFileSnapshot> snapshots;
    std::unordered_set<std::string> wallpaperFiles;
    if (!snapshotStagingFiles(
            callerStaging.get(), callerUid, snapshots, wallpaperFiles, /*allowLegacyManifest=*/true,
            writableEntryPolicy, errorOut
        )) {
      return false;
    }
    callerStaging = UniqueFd{};
    if (!dropPrivileges(target, errorOut)) {
      return false;
    }

    TemporaryDirectory privateStaging;
    if (!privateStaging.create(errorOut) || !materializeSnapshots(privateStaging, snapshots, errorOut)) {
      return false;
    }
    if (!appearance::installFromStaging(privateStaging.path(), errorOut)) {
      return false;
    }
    return appearance::applySyncedGreeterPreferences(privateStaging.path(), /*includeSessionCommands=*/true, errorOut);
  }

} // namespace greeter::secure_sync
