#include "tools/passwordless_sync_policy.h"

#include "tools/passwordless_sync_policy_xml.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <linux/magic.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace greeter::passwordless_sync {

  namespace {

    constexpr char kRulesDirectory[] = "/etc/polkit-1/rules.d";
    constexpr char kManagedRuleName[] = "49-noctalia-greeter-passwordless-sync.rules";
    constexpr char kLockPath[] = "/run/noctalia-greeter-passwordless-sync.lock";
    constexpr char kApplyHelperName[] = "noctalia-greeter-apply-appearance";
    constexpr char kActionId[] = "org.noctalia.greeter.sync-appearance";
    constexpr char kSecureSyncCapability[] = "secure-sync-v1";
    constexpr std::string_view kManagedMarker = "// Managed by noctalia-greeter passwordless-sync. Do not edit.\n";
    constexpr std::string_view kMetadataPrefix = "// metadata: ";
    constexpr std::size_t kMaximumRuleSize = std::size_t{64} * 1024U;
    constexpr std::size_t kMaximumPolicySize = std::size_t{256} * 1024U;
    constexpr std::size_t kMaximumAccountRecordSize = std::size_t{1024} * 1024U;
    constexpr std::size_t kMaximumUsers = 256;
    constexpr std::size_t kMaximumUserNameLength = 256;
    constexpr std::size_t kMaximumPathLength = 4096;

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

    struct ManagedRule {
      std::string helper;
      std::vector<std::string> users;
    };

    enum class ManagedRuleState : std::uint8_t {
      Missing,
      Present,
      Invalid,
    };

    [[nodiscard]] bool setErrnoError(std::string_view operation, std::string& errorOut) {
      errorOut = std::string(operation) + ": " + std::strerror(errno);
      return false;
    }

    void printUsage(std::ostream& out) {
      out
          << "Usage: noctalia-greeter passwordless-sync enable USER\n"
          << "       noctalia-greeter passwordless-sync disable USER\n"
          << "       noctalia-greeter passwordless-sync status [USER]\n\n"
          << "Manage the dedicated Polkit rule for constrained, appearance-only sync.\n"
          << "enable and disable require administrator privileges. The authenticated\n"
          << "sync workflow remains available whether or not this rule is enabled.\n";
    }

    [[nodiscard]] bool validateName(std::string_view name, std::string& errorOut) {
      if (name.empty() || name.size() > kMaximumUserNameLength) {
        errorOut = "username must contain between 1 and 256 bytes";
        return false;
      }
      for (const char raw : name) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch < 0x20U || ch == 0x7fU) {
          errorOut = "username must not contain control characters";
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] bool validateStoredPath(std::string_view path, std::string& errorOut) {
      if (path.empty() || path.size() > kMaximumPathLength || !std::filesystem::path(path).is_absolute()) {
        errorOut = "managed rule contains an invalid helper path";
        return false;
      }
      for (const char raw : path) {
        const auto ch = static_cast<unsigned char>(raw);
        if (ch < 0x20U || ch == 0x7fU) {
          errorOut = "managed rule contains an invalid helper path";
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] bool
    resolveExistingAccount(std::string_view requestedName, std::string& canonicalNameOut, std::string& errorOut) {
      if (!validateName(requestedName, errorOut)) {
        return false;
      }

      std::size_t bufferSize = std::size_t{16} * 1024U;
      const long suggestedSize = ::sysconf(_SC_GETPW_R_SIZE_MAX);
      if (suggestedSize >= 1024 && suggestedSize <= 1024L * 1024L) {
        bufferSize = static_cast<std::size_t>(suggestedSize);
      }

      std::vector<char> buffer(bufferSize);
      struct passwd record{};
      struct passwd* result = nullptr;
      int lookupError = 0;
      const std::string requested(requestedName);
      for (;;) {
        lookupError = ::getpwnam_r(requested.c_str(), &record, buffer.data(), buffer.size(), &result);
        if (lookupError != ERANGE) {
          break;
        }
        if (buffer.size() >= kMaximumAccountRecordSize) {
          errorOut = "account record is unexpectedly large";
          return false;
        }
        buffer.resize(std::min(kMaximumAccountRecordSize, buffer.size() * 2U));
      }

      if (lookupError != 0) {
        errorOut = std::string("failed to look up user '") + requested + "': " + std::strerror(lookupError);
        return false;
      }
      if (result == nullptr || record.pw_name == nullptr) {
        errorOut = std::string("user '") + requested + "' does not exist";
        return false;
      }
      if (record.pw_uid == 0) {
        errorOut = "passwordless sync cannot be enabled for the root account";
        return false;
      }

      canonicalNameOut = record.pw_name;
      if (!validateName(canonicalNameOut, errorOut)) {
        errorOut = "the account database returned an invalid canonical username";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool requireAdministrator(std::string& errorOut) {
      if (::getuid() == 0 && ::geteuid() == 0) {
        return true;
      }
      errorOut =
          "this operation requires a real administrator process; rerun it with sudo or another administrator tool";
      return false;
    }

    [[nodiscard]] bool validateTrustedDirectoryChain(
        const std::filesystem::path& directory, std::string_view description, std::string& errorOut
    ) {
      if (!directory.is_absolute()) {
        errorOut = std::string(description) + " path is not absolute";
        return false;
      }

      for (std::filesystem::path current = directory; !current.empty(); current = current.parent_path()) {
        struct stat state{};
        if (::stat(current.c_str(), &state) != 0) {
          return setErrnoError(std::string("failed to inspect ") + std::string(description) + " directory", errorOut);
        }
        if (!S_ISDIR(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
          errorOut = std::string(description) + " must use a root-owned, non-user-writable directory chain";
          return false;
        }
        if (current == current.root_path()) {
          break;
        }
      }

      struct statfs filesystemState{};
      if (::statfs(directory.c_str(), &filesystemState) != 0) {
        return setErrnoError(std::string("failed to inspect ") + std::string(description) + " filesystem", errorOut);
      }
      if (filesystemState.f_type == FUSE_SUPER_MAGIC) {
        errorOut = std::string(description) + " must not be stored on FUSE";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool trustedExecutable(
        const std::filesystem::path& candidate, std::filesystem::path& canonicalOut, std::string& errorOut
    ) {
      std::error_code ec;
      const std::filesystem::path canonical = std::filesystem::canonical(candidate, ec);
      if (ec) {
        errorOut = "required executable '" + candidate.string() + "' is unavailable: " + ec.message();
        return false;
      }

      struct stat state{};
      if (::stat(canonical.c_str(), &state) != 0) {
        return setErrnoError("failed to inspect executable '" + canonical.string() + "'", errorOut);
      }
      if (!S_ISREG(state.st_mode)
          || state.st_uid != 0
          || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0
          || (state.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        errorOut =
            "executable '" + canonical.string() + "' must be a root-owned, non-user-writable executable regular file";
        return false;
      }
      if (!validateTrustedDirectoryChain(canonical.parent_path(), "executable", errorOut)) {
        return false;
      }

      struct statfs filesystemState{};
      if (::statfs(canonical.c_str(), &filesystemState) != 0) {
        return setErrnoError("failed to inspect executable filesystem", errorOut);
      }
      if (filesystemState.f_type == FUSE_SUPER_MAGIC) {
        errorOut = "executable '" + canonical.string() + "' must not be stored on FUSE";
        return false;
      }

      canonicalOut = canonical;
      return true;
    }

    [[nodiscard]] bool readBoundedFile(
        int fd, std::size_t maximumSize, std::string_view description, std::string& contentsOut, std::string& errorOut
    ) {
      struct stat state{};
      if (::fstat(fd, &state) != 0) {
        return setErrnoError(std::string("failed to inspect ") + std::string(description), errorOut);
      }
      if (!S_ISREG(state.st_mode) || state.st_size < 0 || static_cast<std::uintmax_t>(state.st_size) > maximumSize) {
        errorOut = std::string(description) + " must be a bounded regular file";
        return false;
      }

      contentsOut.clear();
      contentsOut.reserve(static_cast<std::size_t>(state.st_size));
      std::array<char, 4096> buffer{};
      for (;;) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0) {
          if (errno == EINTR) {
            continue;
          }
          return setErrnoError(std::string("failed to read ") + std::string(description), errorOut);
        }
        if (count == 0) {
          break;
        }
        const std::size_t chunkSize = static_cast<std::size_t>(count);
        if (chunkSize > maximumSize - contentsOut.size()) {
          errorOut = std::string(description) + " exceeds its size limit";
          return false;
        }
        contentsOut.append(buffer.data(), chunkSize);
        if (contentsOut.size() > maximumSize) {
          errorOut = std::string(description) + " exceeds its size limit";
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] std::string renderRule(const ManagedRule& rule) {
      const nlohmann::json metadata = {
          {"format", 1},
          {"helper", rule.helper},
          {"users", rule.users},
      };
      const nlohmann::json users = rule.users;

      std::string contents;
      contents.reserve(1024U + rule.users.size() * 64U);
      contents += kManagedMarker;
      contents += kMetadataPrefix;
      contents += metadata.dump(-1, ' ', true);
      contents += "\n\npolkit.addRule(function(action, subject) {\n";
      contents += "  var allowedUsers = ";
      contents += users.dump(-1, ' ', true);
      contents += ";\n\n";
      contents += "  if (action.id == ";
      contents += nlohmann::json(kActionId).dump(-1, ' ', true);
      contents += " &&\n      action.lookup(\"program\") == ";
      contents += nlohmann::json(rule.helper).dump(-1, ' ', true);
      contents += " &&\n      action.lookup(\"user\") == \"root\" &&\n";
      contents += "      subject.local && subject.active &&\n";
      contents += "      allowedUsers.indexOf(subject.user) >= 0) {\n";
      contents += "    return polkit.Result.YES;\n  }\n});\n";
      return contents;
    }

    [[nodiscard]] bool parseManagedRule(std::string_view contents, ManagedRule& ruleOut, std::string& errorOut) {
      if (!contents.starts_with(kManagedMarker)) {
        errorOut = "the existing rule is not managed by noctalia-greeter";
        return false;
      }

      const std::size_t metadataStart = kManagedMarker.size();
      if (contents.substr(metadataStart).starts_with(kMetadataPrefix)) {
        const std::size_t jsonStart = metadataStart + kMetadataPrefix.size();
        const std::size_t jsonEnd = contents.find('\n', jsonStart);
        if (jsonEnd == std::string_view::npos) {
          errorOut = "the managed rule metadata is incomplete";
          return false;
        }

        try {
          const nlohmann::json metadata = nlohmann::json::parse(contents.substr(jsonStart, jsonEnd - jsonStart));
          if (!metadata.is_object()
              || metadata.size() != 3
              || !metadata.contains("format")
              || !metadata["format"].is_number_integer()
              || metadata["format"].get<int>() != 1
              || !metadata.contains("helper")
              || !metadata["helper"].is_string()
              || !metadata.contains("users")
              || !metadata["users"].is_array()) {
            errorOut = "the managed rule metadata has an unsupported format";
            return false;
          }

          ManagedRule parsed;
          parsed.helper = metadata["helper"].get<std::string>();
          if (!validateStoredPath(parsed.helper, errorOut)) {
            return false;
          }
          if (metadata["users"].size() > kMaximumUsers) {
            errorOut = "the managed rule contains too many users";
            return false;
          }
          for (const nlohmann::json& value : metadata["users"]) {
            if (!value.is_string()) {
              errorOut = "the managed rule contains an invalid user list";
              return false;
            }
            parsed.users.push_back(value.get<std::string>());
            if (!validateName(parsed.users.back(), errorOut)) {
              errorOut = "the managed rule contains an invalid username";
              return false;
            }
          }
          if (!std::is_sorted(parsed.users.begin(), parsed.users.end())
              || std::adjacent_find(parsed.users.begin(), parsed.users.end()) != parsed.users.end()) {
            errorOut = "the managed rule user list is not canonical";
            return false;
          }
          if (renderRule(parsed) != contents) {
            errorOut = "the managed rule was modified outside noctalia-greeter";
            return false;
          }
          ruleOut = std::move(parsed);
          return true;
        } catch (const nlohmann::json::exception&) {
          errorOut = "the managed rule metadata is invalid JSON";
          return false;
        }
      }

      errorOut = "the managed rule metadata is missing";
      return false;
    }

    [[nodiscard]] bool openRulesDirectory(UniqueFd& directoryOut, std::string& errorOut) {
      std::error_code ec;
      const std::filesystem::path canonical = std::filesystem::canonical(kRulesDirectory, ec);
      if (ec) {
        errorOut = "Polkit rules directory '"
            + std::string(kRulesDirectory)
            + "' is unavailable; install and enable Polkit: "
            + ec.message();
        return false;
      }
      if (!validateTrustedDirectoryChain(canonical, "Polkit rules", errorOut)) {
        return false;
      }

      UniqueFd directory(::open(canonical.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (!directory.valid()) {
        return setErrnoError("failed to open the Polkit rules directory", errorOut);
      }
      struct stat state{};
      if (::fstat(directory.get(), &state) != 0) {
        return setErrnoError("failed to inspect the Polkit rules directory", errorOut);
      }
      if (!S_ISDIR(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "the Polkit rules directory has unsafe ownership or permissions";
        return false;
      }
      directoryOut = std::move(directory);
      return true;
    }

    [[nodiscard]] ManagedRuleState readManagedRule(int directoryFd, ManagedRule& ruleOut, std::string& errorOut) {
      UniqueFd file(::openat(directoryFd, kManagedRuleName, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
      if (!file.valid()) {
        if (errno == ENOENT) {
          return ManagedRuleState::Missing;
        }
        (void)setErrnoError("failed to open the managed passwordless sync rule", errorOut);
        return ManagedRuleState::Invalid;
      }

      struct stat state{};
      if (::fstat(file.get(), &state) != 0) {
        (void)setErrnoError("failed to inspect the managed passwordless sync rule", errorOut);
        return ManagedRuleState::Invalid;
      }
      if (!S_ISREG(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "the managed passwordless sync rule has unsafe type, ownership, or permissions";
        return ManagedRuleState::Invalid;
      }

      std::string contents;
      if (!readBoundedFile(file.get(), kMaximumRuleSize, "managed passwordless sync rule", contents, errorOut)
          || !parseManagedRule(contents, ruleOut, errorOut)) {
        return ManagedRuleState::Invalid;
      }
      return ManagedRuleState::Present;
    }

    [[nodiscard]] bool acquireUpdateLock(UniqueFd& lockOut, std::string& errorOut) {
      UniqueFd lock(::open(kLockPath, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR));
      if (!lock.valid()) {
        return setErrnoError("failed to open the passwordless sync update lock", errorOut);
      }
      struct stat state{};
      if (::fstat(lock.get(), &state) != 0) {
        return setErrnoError("failed to inspect the passwordless sync update lock", errorOut);
      }
      if (!S_ISREG(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        errorOut = "the passwordless sync update lock has unsafe type, ownership, or permissions";
        return false;
      }
      if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          errorOut = "another passwordless sync update is already in progress";
          return false;
        }
        return setErrnoError("failed to lock the passwordless sync update", errorOut);
      }
      lockOut = std::move(lock);
      return true;
    }

    [[nodiscard]] bool writeAll(int fd, std::string_view contents, std::string& errorOut) {
      std::size_t offset = 0;
      while (offset < contents.size()) {
        const ssize_t written = ::write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0) {
          if (errno == EINTR) {
            continue;
          }
          return setErrnoError("failed to write the temporary Polkit rule", errorOut);
        }
        if (written == 0) {
          errorOut = "failed to write the temporary Polkit rule: short write";
          return false;
        }
        offset += static_cast<std::size_t>(written);
      }
      return true;
    }

    [[nodiscard]] bool installManagedRule(int directoryFd, const ManagedRule& rule, std::string& errorOut) {
      const std::string contents = renderRule(rule);
      if (contents.size() > kMaximumRuleSize) {
        errorOut = "the managed passwordless sync rule would exceed its size limit";
        return false;
      }
      std::string temporaryName;
      UniqueFd temporary;
      for (unsigned int attempt = 0; attempt < 100U; ++attempt) {
        temporaryName =
            ".49-noctalia-greeter-passwordless-sync.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(attempt);
        temporary = UniqueFd(
            ::openat(
                directoryFd, temporaryName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                S_IRUSR | S_IWUSR
            )
        );
        if (temporary.valid() || errno != EEXIST) {
          break;
        }
      }
      if (!temporary.valid()) {
        return setErrnoError("failed to create a temporary Polkit rule", errorOut);
      }

      bool renamed = false;
      const auto cleanup = [&]() {
        if (!renamed) {
          ::unlinkat(directoryFd, temporaryName.c_str(), 0);
        }
      };

      if (!writeAll(temporary.get(), contents, errorOut)) {
        cleanup();
        return false;
      }
      if (::fchown(temporary.get(), 0, 0) != 0
          || ::fchmod(temporary.get(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0
          || ::fsync(temporary.get()) != 0) {
        (void)setErrnoError("failed to finalize the temporary Polkit rule", errorOut);
        cleanup();
        return false;
      }
      if (::renameat(directoryFd, temporaryName.c_str(), directoryFd, kManagedRuleName) != 0) {
        (void)setErrnoError("failed to atomically install the Polkit rule", errorOut);
        cleanup();
        return false;
      }
      renamed = true;
      if (::fsync(directoryFd) != 0) {
        return setErrnoError("failed to persist the Polkit rules directory update", errorOut);
      }
      return true;
    }

    [[nodiscard]] bool removeManagedRule(int directoryFd, std::string& errorOut) {
      if (::unlinkat(directoryFd, kManagedRuleName, 0) != 0) {
        if (errno == ENOENT) {
          return true;
        }
        return setErrnoError("failed to remove the managed passwordless sync rule", errorOut);
      }
      if (::fsync(directoryFd) != 0) {
        return setErrnoError("failed to persist the Polkit rules directory update", errorOut);
      }
      return true;
    }

    [[nodiscard]] bool validateInstalledPolicy(const std::filesystem::path& helper, std::string& errorOut) {
      const std::filesystem::path configuredPath(NOCTALIA_GREETER_INSTALLED_POLICY_PATH);
      struct stat linkState{};
      if (::lstat(configuredPath.c_str(), &linkState) != 0) {
        return setErrnoError(
            "the packaged Polkit policy is unavailable at '" + configuredPath.string() + "'", errorOut
        );
      }
      if (!S_ISREG(linkState.st_mode)) {
        errorOut = "the packaged Polkit policy must be a regular file, not a symlink";
        return false;
      }

      std::error_code ec;
      const std::filesystem::path canonical = std::filesystem::canonical(configuredPath, ec);
      if (ec || !validateTrustedDirectoryChain(canonical.parent_path(), "packaged Polkit policy", errorOut)) {
        if (ec) {
          errorOut = "failed to resolve the packaged Polkit policy: " + ec.message();
        }
        return false;
      }

      UniqueFd policy(::open(canonical.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
      if (!policy.valid()) {
        return setErrnoError("failed to open the packaged Polkit policy", errorOut);
      }
      struct stat state{};
      if (::fstat(policy.get(), &state) != 0) {
        return setErrnoError("failed to inspect the packaged Polkit policy", errorOut);
      }
      if (!S_ISREG(state.st_mode) || state.st_uid != 0 || (state.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errorOut = "the packaged Polkit policy has unsafe ownership or permissions";
        return false;
      }

      std::string contents;
      if (!readBoundedFile(policy.get(), kMaximumPolicySize, "packaged Polkit policy", contents, errorOut)) {
        return false;
      }
      if (!detail::validateConstrainedPolicyXml(contents, helper)) {
        errorOut = "the packaged Polkit policy does not constrain the installed helper to --sync";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool probeHelperCapability(const std::filesystem::path& helper, std::string& errorOut) {
      std::array<int, 2> pipeFds{};
      if (::pipe2(pipeFds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        return setErrnoError("failed to create the helper capability pipe", errorOut);
      }
      UniqueFd readEnd(pipeFds[0]);
      UniqueFd writeEnd(pipeFds[1]);

      const pid_t child = ::fork();
      if (child < 0) {
        return setErrnoError("failed to start the installed apply helper", errorOut);
      }
      if (child == 0) {
        if (::dup2(writeEnd.get(), STDOUT_FILENO) < 0 || ::dup2(writeEnd.get(), STDERR_FILENO) < 0) {
          _exit(126);
        }
        readEnd = UniqueFd();
        writeEnd = UniqueFd();
        std::string helperPath = helper.string();
        std::string supportsArgument = "--supports";
        std::string capabilityArgument = kSecureSyncCapability;
        std::array<char*, 4> arguments{
            helperPath.data(),
            supportsArgument.data(),
            capabilityArgument.data(),
            nullptr,
        };
        ::execv(helperPath.c_str(), arguments.data());
        _exit(127);
      }

      writeEnd = UniqueFd();
      std::string output;
      std::array<char, 256> buffer{};
      bool pipeClosed = false;
      bool timedOut = false;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (!pipeClosed && errorOut.empty()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          timedOut = true;
          break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        struct pollfd descriptor{readEnd.get(), POLLIN | POLLHUP, 0};
        const int pollResult = ::poll(&descriptor, 1, static_cast<int>(std::max<std::int64_t>(remaining, 1)));
        if (pollResult < 0) {
          if (errno != EINTR) {
            errorOut = std::string("failed to poll the helper capability response: ") + std::strerror(errno);
          }
          continue;
        }
        if (pollResult == 0) {
          timedOut = true;
          break;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
          errorOut = "failed while polling the helper capability response";
          break;
        }
        for (;;) {
          const ssize_t count = ::read(readEnd.get(), buffer.data(), buffer.size());
          if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
            if (output.size() > 4096U) {
              errorOut = "the installed apply helper returned an oversized capability response";
              break;
            }
            continue;
          }
          if (count == 0) {
            pipeClosed = true;
            break;
          }
          if (errno == EINTR) {
            continue;
          }
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if ((descriptor.revents & POLLHUP) != 0) {
              pipeClosed = true;
            }
            break;
          }
          errorOut = std::string("failed to read the helper capability response: ") + std::strerror(errno);
          break;
        }
      }

      int status = 0;
      bool childReaped = false;
      while (!timedOut && errorOut.empty() && !childReaped) {
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
          childReaped = true;
          break;
        }
        if (waited < 0) {
          if (errno == EINTR) {
            continue;
          }
          return setErrnoError("failed to wait for the installed apply helper", errorOut);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          timedOut = true;
          break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int delay = static_cast<int>(std::min<std::int64_t>(remaining, 10));
        if (::poll(nullptr, 0, std::max(delay, 1)) < 0 && errno != EINTR) {
          errorOut = std::string("failed while waiting for the helper capability probe: ") + std::strerror(errno);
        }
      }

      if (!childReaped) {
        readEnd = UniqueFd();
        if (::kill(child, SIGKILL) != 0 && errno != ESRCH && errorOut.empty()) {
          errorOut = std::string("failed to stop the helper capability probe: ") + std::strerror(errno);
        }
        pid_t waited = -1;
        do {
          waited = ::waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
          return setErrnoError("failed to reap the installed apply helper", errorOut);
        }
      }
      if (timedOut) {
        errorOut = "the installed apply helper capability probe timed out";
        return false;
      }
      if (!errorOut.empty()) {
        return false;
      }
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || output != std::string(kSecureSyncCapability) + "\n") {
        errorOut = "the installed apply helper does not support constrained passwordless sync";
        return false;
      }
      return true;
    }

    [[nodiscard]] bool findTrustedPkexec(std::string& errorOut) {
      constexpr std::array<std::string_view, 3> candidates{
          "/usr/bin/pkexec",
          "/bin/pkexec",
          "/usr/local/bin/pkexec",
      };
      for (const std::string_view candidate : candidates) {
        std::filesystem::path canonical;
        std::string ignored;
        if (trustedExecutable(candidate, canonical, ignored)) {
          struct stat state{};
          if (::stat(canonical.c_str(), &state) == 0 && (state.st_mode & S_ISUID) != 0) {
            return true;
          }
        }
      }
      errorOut = "a trusted setuid-root pkexec executable was not found; install and enable Polkit";
      return false;
    }

    [[nodiscard]] bool validateInstalledSync(std::string& helperOut, std::string& errorOut) {
      std::error_code ec;
      const std::filesystem::path self = std::filesystem::canonical("/proc/self/exe", ec);
      if (ec) {
        errorOut = "failed to resolve the installed noctalia-greeter executable: " + ec.message();
        return false;
      }
      std::filesystem::path helper;
      if (!trustedExecutable(self.parent_path() / kApplyHelperName, helper, errorOut)) {
        errorOut += "; install noctalia-greeter into a trusted system prefix before enabling passwordless sync";
        return false;
      }
      if (!probeHelperCapability(helper, errorOut)
          || !validateInstalledPolicy(helper, errorOut)
          || !findTrustedPkexec(errorOut)) {
        return false;
      }
      helperOut = helper.string();
      return true;
    }

    [[nodiscard]] int reportError(const std::string& error) {
      std::cerr << "error: " << error << '\n';
      return 1;
    }

    [[nodiscard]] int enableForUser(std::string_view requestedUser) {
      std::string error;
      if (!requireAdministrator(error)) {
        return reportError(error);
      }

      std::string user;
      if (!resolveExistingAccount(requestedUser, user, error)) {
        return reportError(error);
      }

      std::string helper;
      if (!validateInstalledSync(helper, error)) {
        return reportError(error);
      }

      UniqueFd rulesDirectory;
      if (!openRulesDirectory(rulesDirectory, error)) {
        return reportError(error);
      }
      UniqueFd lock;
      if (!acquireUpdateLock(lock, error)) {
        return reportError(error);
      }

      ManagedRule rule;
      const ManagedRuleState state = readManagedRule(rulesDirectory.get(), rule, error);
      if (state == ManagedRuleState::Invalid) {
        return reportError(error + "; refusing to overwrite it");
      }
      if (state == ManagedRuleState::Missing) {
        rule.helper = helper;
      }

      const bool alreadyEnabled = std::binary_search(rule.users.begin(), rule.users.end(), user);
      const bool helperChanged = rule.helper != helper;
      if (alreadyEnabled && !helperChanged) {
        std::cout << "Passwordless appearance sync is already enabled for '" << user << "'.\n";
        return 0;
      }
      if (!alreadyEnabled) {
        if (rule.users.size() >= kMaximumUsers) {
          return reportError("the managed passwordless sync rule already contains the maximum number of users");
        }
        rule.users.push_back(user);
        std::sort(rule.users.begin(), rule.users.end());
      }
      rule.helper = helper;
      if (!installManagedRule(rulesDirectory.get(), rule, error)) {
        return reportError(error);
      }

      std::cout
          << (alreadyEnabled ? "Refreshed" : "Enabled")
          << " passwordless appearance sync for '"
          << user
          << "'.\nThe noctalia-greeter managed rule authorizes only the constrained appearance sync action, and only "
             "from an active local session.\n";
      return 0;
    }

    [[nodiscard]] int disableForUser(std::string_view user) {
      std::string error;
      if (!requireAdministrator(error) || !validateName(user, error)) {
        return reportError(error);
      }

      UniqueFd rulesDirectory;
      if (!openRulesDirectory(rulesDirectory, error)) {
        return reportError(error);
      }
      UniqueFd lock;
      if (!acquireUpdateLock(lock, error)) {
        return reportError(error);
      }

      ManagedRule rule;
      const ManagedRuleState state = readManagedRule(rulesDirectory.get(), rule, error);
      if (state == ManagedRuleState::Invalid) {
        return reportError(error + "; refusing to modify it");
      }
      if (state == ManagedRuleState::Missing) {
        std::cout << "Passwordless appearance sync is not enabled by this command for '" << user << "'.\n";
        return 0;
      }

      const auto found = std::lower_bound(rule.users.begin(), rule.users.end(), user);
      if (found == rule.users.end() || *found != user) {
        std::cout << "Passwordless appearance sync is not enabled by this command for '" << user << "'.\n";
        return 0;
      }
      rule.users.erase(found);
      const bool removedRule = rule.users.empty();
      const bool updated = removedRule ? removeManagedRule(rulesDirectory.get(), error)
                                       : installManagedRule(rulesDirectory.get(), rule, error);
      if (!updated) {
        return reportError(error);
      }

      std::cout << "Removed '" << user << "' from the passwordless sync rule managed by noctalia-greeter.\n";
      if (removedRule) {
        std::cout << "Removed the now-empty rule managed by noctalia-greeter.\n";
      }
      std::cout << "Administrator-authenticated appearance sync remains available. Other Polkit rules may still "
                   "authorize this user.\n";
      return 0;
    }

    [[nodiscard]] int showStatus(const std::optional<std::string_view> requestedUser) {
      std::string error;
      if (requestedUser.has_value() && !validateName(*requestedUser, error)) {
        return reportError(error);
      }

      UniqueFd rulesDirectory;
      if (!openRulesDirectory(rulesDirectory, error)) {
        if (::geteuid() != 0) {
          error += "; rerun status with sudo if this directory is not readable by your user";
        }
        return reportError(error);
      }

      ManagedRule rule;
      const ManagedRuleState state = readManagedRule(rulesDirectory.get(), rule, error);
      if (state == ManagedRuleState::Invalid) {
        if (::geteuid() != 0) {
          error += "; rerun status with sudo if the rule is not readable by your user";
        }
        return reportError(error);
      }
      if (requestedUser.has_value()) {
        const bool enabled = state == ManagedRuleState::Present
            && std::binary_search(rule.users.begin(), rule.users.end(), *requestedUser);
        std::cout
            << *requestedUser
            << ": "
            << (enabled ? "enabled" : "not enabled")
            << " by the noctalia-greeter managed rule\n"
            << "Other administrator-authored Polkit rules are not included in this status.\n";
        return 0;
      }
      if (state == ManagedRuleState::Missing) {
        std::cout << "No passwordless sync users are enabled by the noctalia-greeter managed rule.\n";
        return 0;
      }

      std::cout << "Passwordless appearance sync users managed by noctalia-greeter:\n";
      for (const std::string& user : rule.users) {
        std::cout << "  " << user << '\n';
      }
      std::cout
          << "Helper: "
          << rule.helper
          << "\n"
          << "Other administrator-authored Polkit rules are not included in this status.\n";
      return 0;
    }

  } // namespace

  int runCommand(const int argc, char* argv[]) {
    if (argc == 1 && (std::string_view(argv[0]) == "--help" || std::string_view(argv[0]) == "-h")) {
      printUsage(std::cout);
      return 0;
    }
    if (argc == 2 && std::string_view(argv[0]) == "enable") {
      return enableForUser(argv[1]);
    }
    if (argc == 2 && std::string_view(argv[0]) == "disable") {
      return disableForUser(argv[1]);
    }
    if ((argc == 1 || argc == 2) && std::string_view(argv[0]) == "status") {
      return showStatus(argc == 2 ? std::optional<std::string_view>(argv[1]) : std::nullopt);
    }

    printUsage(std::cerr);
    return 2;
  }

} // namespace greeter::passwordless_sync
