#include "greeter/greeter_config_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace {

  class Fixture {
  public:
    Fixture() {
      char pathTemplate[] = "/tmp/noctalia-greeter-config-test.XXXXXX";
      const int fd = ::mkstemp(pathTemplate);
      if (fd < 0) {
        throw std::runtime_error("mkstemp failed");
      }
      ::close(fd);
      path = pathTemplate;
    }

    ~Fixture() {
      std::error_code error;
      std::filesystem::remove(path, error);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    std::filesystem::path path;
  };

  bool expect(std::string_view name, const std::optional<bool>& actual, const bool expected) {
    if (actual.has_value() && *actual == expected) {
      return true;
    }
    std::cerr << name << ": expected " << expected << "\n";
    return false;
  }

} // namespace

int main() {
  try {
    Fixture fixture;
    std::ofstream(fixture.path) << R"toml(
[appearance]
hide_session_selector = true
hide_scheme_selector = false
hide_shutdown_button = true
hide_reboot_button = false
hide_firmware_button = true
)toml";

    const auto config = greeter::config::loadConfig(fixture.path);
    bool passed = true;
    passed &= expect("hide_session_selector", config.appearanceHideSessionSelector, true);
    passed &= expect("hide_scheme_selector", config.appearanceHideSchemeSelector, false);
    passed &= expect("hide_shutdown_button", config.appearanceHideShutdownButton, true);
    passed &= expect("hide_reboot_button", config.appearanceHideRebootButton, false);
    passed &= expect("hide_firmware_button", config.appearanceHideFirmwareButton, true);
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
