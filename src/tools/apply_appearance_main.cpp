#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greetd_user.h"
#include "greeter/greeter_preferences.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

  constexpr Logger kLog("apply-appearance");

  void printUsage(const char* programName) {
    std::cerr
        << "usage: "
        << programName
        << " <staging-directory>\n"
        << "       "
        << programName
        << " --setup-system\n"
        << "       "
        << programName
        << " --print-greeter-user\n\n"
        << "Installs appearance into "
        << greeter::appearance::syncedDataDirectory().string()
        << " (root, world-readable).\n"
        << "--setup-system creates greeter.conf and chowns it to the "
           "greetd user.\n\n"
        << "Environment:\n"
        << "  "
        << greeter::appearance::kSyncedDataDirEnv
        << "  synced data dir\n"
        << "  "
        << greeter::kGreeterUserEnv
        << "  greeter user\n"
        << "  GREETD_CONFIG  greetd config.toml path\n";
  }

} // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view(argv[1]) == "--setup-system") {
    const auto greeterUser = greeter::resolveGreeterAccountName();
    if (!greeterUser.has_value()) {
      kLog.error("could not resolve greeter account from greetd config");
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
      kLog.error("could not resolve greeter account from greetd config");
      return 1;
    }
    std::cout << *greeterUser << '\n';
    return 0;
  }

  if (argc != 2) {
    printUsage(argv[0] != nullptr ? argv[0] : "noctalia-greeter-apply-appearance");
    return 2;
  }

  const std::filesystem::path stagingDirectory = argv[1];
  std::string error;
  if (!greeter::appearance::installFromStaging(stagingDirectory, error)) {
    kLog.error("{}", error);
    return 1;
  }

  greeter::GreeterPreferences prefs = greeter::loadGreeterPreferences();
  prefs.scheme = greeter::appearance::kSyncedSchemeDisplayName;
  if (!greeter::saveGreeterPreferences(prefs)) {
    kLog.error("installed appearance but failed to set scheme in greeter.conf");
    return 1;
  }

  kLog.info("installed appearance into '{}'", greeter::appearance::syncedDataDirectory().string());
  return 0;
}
