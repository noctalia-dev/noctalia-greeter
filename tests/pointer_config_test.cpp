#include "compositor/pointer_config.h"
#include "greeter/greeter_config_io.h"
#include "greeter/greeter_config_store.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <libinput.h>
#include <stdexcept>
#include <string>
#include <string_view>

// Observe the libinput API boundary without changing a real input device.
struct libinput_device {
  bool available = true;
  std::uint32_t profiles = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT | LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
  libinput_config_accel_profile profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
  double speed = 0.25;
  unsigned profileCalls = 0;
  unsigned speedCalls = 0;
  libinput_config_status profileStatus = LIBINPUT_CONFIG_STATUS_SUCCESS;
  libinput_config_status speedStatus = LIBINPUT_CONFIG_STATUS_SUCCESS;
};

extern "C" int libinput_device_config_accel_is_available(libinput_device* device) { return device->available; }

extern "C" std::uint32_t libinput_device_config_accel_get_profiles(libinput_device* device) { return device->profiles; }

extern "C" libinput_config_status
libinput_device_config_accel_set_profile(libinput_device* device, libinput_config_accel_profile profile) {
  ++device->profileCalls;
  if (device->profileStatus == LIBINPUT_CONFIG_STATUS_SUCCESS) {
    device->profile = profile;
  }
  return device->profileStatus;
}

extern "C" libinput_config_status libinput_device_config_accel_set_speed(libinput_device* device, double speed) {
  ++device->speedCalls;
  if (device->speedStatus == LIBINPUT_CONFIG_STATUS_SUCCESS) {
    device->speed = speed;
  }
  return device->speedStatus;
}

namespace {
  bool passed = true;

  void expect(bool condition, const char* label) {
    if (!condition) {
      std::fprintf(stderr, "%s\n", label);
      passed = false;
    }
  }

  class Fixture {
  public:
    Fixture() {
      char pattern[] = "/tmp/noctalia-pointer-config-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
      }
      directory = created;
    }

    ~Fixture() {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }

    greeter_compositor_config load(std::string_view content) {
      std::ofstream(directory / "greeter.toml") << content;
      greeter_compositor_config config{};
      greeter_compositor_config_load(directory.c_str(), &config);
      return config;
    }

    std::filesystem::path directory;
  };

  void apply(libinput_device& device, const greeter_compositor_config& config) {
    pointer_config_apply(
        &device, "synthetic pointer", config.pointer_accel_profile,
        config.pointer_accel_speed_set ? &config.pointer_accel_speed : nullptr
    );
  }

  void testDefaults(Fixture& fixture) {
    const auto config = fixture.load("");
    libinput_device device;
    apply(device, config);
    expect(device.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "omitted profile must keep the legacy flat default");
    expect(device.speedCalls == 0 && device.speed == 0.25, "omitted speed must leave libinput speed unchanged");
  }

  void testConfiguredPointer(Fixture& fixture) {
    for (const auto profile : {"flat", "adaptive"}) {
      for (const auto speed : {"-1.0", "-0.4", "0", "0.6", "1.0"}) {
        const std::string content =
            std::string("[pointer]\naccel_profile = \"") + profile + "\"\naccel_speed = " + speed + '\n';
        const auto config = fixture.load(content);
        expect(
            std::string_view(config.pointer_accel_profile) == profile, "configured acceleration profile was ignored"
        );
        expect(config.pointer_accel_speed_set != 0, "configured acceleration speed was ignored");
        libinput_device device;
        apply(device, config);
        const auto expectedProfile = std::string_view(profile) == "flat" ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
                                                                         : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        expect(device.profile == expectedProfile && device.profileCalls == 1, "wrong profile sent to libinput");
        expect(
            device.speedCalls == 1 && std::abs(device.speed - std::stod(speed)) < 1e-9, "wrong speed sent to libinput"
        );

        const auto parsed = greeter::config::loadConfig(fixture.directory / "greeter.toml");
        expect(greeter::config::writeConfig(fixture.directory / "roundtrip.toml", parsed), "config write failed");
        const auto reloaded = greeter::config::loadConfig(fixture.directory / "roundtrip.toml");
        expect(reloaded.pointerAccelProfile == profile, "config writer lost the pointer profile");
        expect(reloaded.pointerAccelSpeed == std::stod(speed), "config writer lost the pointer speed");
      }
    }
  }

  void testInvalidSettings(Fixture& fixture) {
    for (const auto value : {"-1.01", "1.01", "nan", "inf", "-inf", "true", "\"0.5\""}) {
      const auto config = fixture.load(std::string("[pointer]\naccel_speed = ") + value + '\n');
      expect(config.pointer_accel_speed_set == 0, "invalid speed was accepted");
      libinput_device device;
      apply(device, config);
      expect(device.speedCalls == 0 && device.speed == 0.25, "invalid speed changed the device");
    }
    for (const auto value : {"\"unknown\"", "\"\"", "42", "true"}) {
      const auto config = fixture.load(std::string("[pointer]\naccel_profile = ") + value + '\n');
      expect(config.pointer_accel_profile[0] == '\0', "invalid profile was accepted");
      libinput_device device;
      apply(device, config);
      expect(device.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "invalid profile lost the legacy default");
    }
  }

  void testIndependentSettings(Fixture& fixture) {
    libinput_device profileOnly;
    apply(profileOnly, fixture.load("[pointer]\naccel_profile = \"adaptive\"\n"));
    expect(
        profileOnly.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE && profileOnly.speedCalls == 0,
        "profile-only override changed device speed"
    );

    libinput_device speedOnly;
    apply(speedOnly, fixture.load("[pointer]\naccel_speed = -0.2\n"));
    expect(
        speedOnly.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT && speedOnly.speed == -0.2,
        "speed-only override lost the flat default or speed"
    );

    libinput_device invalidProfile;
    apply(invalidProfile, fixture.load("[pointer]\naccel_profile = \"invalid\"\naccel_speed = 0.4\n"));
    expect(
        invalidProfile.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT && invalidProfile.speed == 0.4,
        "invalid profile discarded a valid independent speed"
    );
  }

  void testUnsupportedDevices(Fixture& fixture) {
    const auto config = fixture.load("[pointer]\naccel_profile = \"adaptive\"\naccel_speed = 0.7\n");
    libinput_device unavailable;
    unavailable.available = false;
    apply(unavailable, config);
    expect(unavailable.profileCalls == 0 && unavailable.speedCalls == 0, "device without acceleration was configured");

    libinput_device flatOnly;
    flatOnly.profiles = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    flatOnly.profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    apply(flatOnly, config);
    expect(flatOnly.profileCalls == 0, "unsupported acceleration profile was applied");
    expect(flatOnly.speedCalls == 1 && flatOnly.speed == 0.7, "unsupported profile prevented speed configuration");

    libinput_device rejectedProfile;
    rejectedProfile.profileStatus = LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
    apply(rejectedProfile, config);
    expect(rejectedProfile.speedCalls == 1, "profile failure prevented independent speed configuration");
    libinput_device rejectedSpeed;
    rejectedSpeed.speedStatus = LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
    apply(rejectedSpeed, config);
    expect(
        rejectedSpeed.profileCalls == 1 && rejectedSpeed.speed == 0.25, "speed failure changed the profile or speed"
    );
  }

  void testSyncDoesNotOverride(Fixture& fixture) {
    std::ofstream(fixture.directory / "sync.toml") << "[pointer]\naccel_profile = \"flat\"\naccel_speed = -1.0\n";
    const auto config = fixture.load("[pointer]\naccel_profile = \"adaptive\"\naccel_speed = 0.3\n");
    libinput_device device;
    apply(device, config);
    expect(
        device.profile == LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE && device.speed == 0.3,
        "sync overrode pointer settings"
    );
  }
} // namespace

int main() {
  try {
    Fixture fixture;
    testDefaults(fixture);
    testConfiguredPointer(fixture);
    testInvalidSettings(fixture);
    testIndependentSettings(fixture);
    testUnsupportedDevices(fixture);
    testSyncDoesNotOverride(fixture);
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "pointer configuration: %s\n", error.what());
    return 1;
  }
}
