#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <sys/types.h>

namespace greeter::secure_sync {

  // Applies the Polkit-authorized appearance-only sync after snapshotting bounded
  // caller-owned files, dropping to the greeter account, and parsing them there.
  [[nodiscard]] bool applyFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut);

  // Retains the administrator-authenticated positional protocol, including
  // session commands, while ensuring root only snapshots bounded caller files.
  [[nodiscard]] bool applyLegacyFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut);

#ifdef NOCTALIA_GREETER_TESTING
  namespace detail {

    // Exercises the same descriptor-based legacy staging validation as production,
    // with an injectable runtime parent so the compatibility policy needs no root setup.
    [[nodiscard]] bool validateLegacyStagingForTesting(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath,
        uid_t runtimeParentOwner, std::optional<uid_t> invokingUid, std::string& errorOut
    );

    [[nodiscard]] bool validateConstrainedStagingForTesting(
        const std::filesystem::path& requestedPath, const std::filesystem::path& runtimeParentPath,
        uid_t runtimeParentOwner, uid_t callerUid, std::string& errorOut
    );

  } // namespace detail
#endif

} // namespace greeter::secure_sync
