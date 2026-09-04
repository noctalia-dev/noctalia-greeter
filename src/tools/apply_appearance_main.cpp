#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greetd_user.h"
#include "greeter/greeter_preferences.h"
#include "tools/secure_appearance_sync.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

  constexpr Logger kLog("apply-appearance");
  constexpr std::string_view kSecureSyncCapability = "secure-sync-v1";

  void printUsage(const char* programName) {
    std::cerr
        << "usage: "
        << programName
        << " <staging-directory>\n"
        << "       "
        << programName
        << " --sync <staging-directory>\n"
        << "       "
        << programName
        << " --supports secure-sync-v1\n"
        << "       "
        << programName
        << " --setup-system\n"
        << "       "
        << programName
        << " --print-greeter-user\n\n"
        << "Installs appearance into "
        << greeter::appearance::syncedDataDirectory().string()
        << " (owned by the greetd session user).\n"
        << "Sync validates and merges into sync.toml as the greetd session user.\n"
        << "--setup-system creates greeter.toml / sync.toml and chowns them to the "
           "greetd user.\n\n"
        << "Environment:\n"
        << "  "
        << greeter::appearance::kSyncedDataDirEnv
        << "  synced data dir (setup/legacy modes; --sync always uses "
        << greeter::appearance::kDefaultSyncedDataDir
        << ")\n"
        << "  "
        << greeter::kGreeterUserEnv
        << "  greeter user\n"
        << "  GREETD_CONFIG  greetd config.toml path\n";
  }

} // namespace

int main(int argc, char* argv[]) {
  if (argc == 3 && std::string_view(argv[1]) == "--supports" && std::string_view(argv[2]) == kSecureSyncCapability) {
    std::cout << kSecureSyncCapability << '\n';
    return 0;
  }

  if (argc >= 2 && std::string_view(argv[1]) == "--sync") {
    if (argc != 3) {
      printUsage(argv[0] != nullptr ? argv[0] : "noctalia-greeter-apply-appearance");
      return 2;
    }
    std::string error;
    if (!greeter::secure_sync::applyFromStaging(argv[2], error)) {
      kLog.error("{}", error);
      return 1;
    }
    kLog.info("installed appearance into '{}'", greeter::appearance::syncedDataDirectory().string());
    return 0;
  }

  if (argc == 2 && std::string_view(argv[1]) == "--setup-system") {
    const auto greeterUser = greeter::resolveGreeterAccountName();
    if (!greeterUser.has_value()) {
      kLog.error("could not resolve greeter account");
      return 1;
    }
    std::string error;
    if (!greeter::installGreeterSystemLayout(*greeterUser, error)) {
      kLog.error("{}", error);
      return 1;
    }
    kLog.info("system layout ready under '{}'", greeter::appearance::syncedDataDirectory().string());
    return 0;
  }

  if (argc == 2 && std::string_view(argv[1]) == "--print-greeter-user") {
    const auto greeterUser = greeter::resolveGreeterAccountName();
    if (!greeterUser.has_value()) {
      kLog.error("could not resolve greeter account");
      return 1;
    }
    std::cout << *greeterUser << '\n';
    return 0;
  }

  if (argc != 2) {
    printUsage(argv[0] != nullptr ? argv[0] : "noctalia-greeter-apply-appearance");
    return 2;
  }

  std::string error;
  if (!greeter::secure_sync::applyLegacyFromStaging(argv[1], error)) {
    kLog.error("{}", error);
    return 1;
  }

  kLog.info("installed appearance and session actions into '{}'", greeter::appearance::syncedDataDirectory().string());
  return 0;
}
