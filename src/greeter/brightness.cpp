#include "greeter/brightness.h"

#include "core/key_symbols.h"
#include "core/log.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gio/gio.h>
#include <optional>
#include <string>
#include <vector>

namespace {
  constexpr Logger kLog("brightness");

  constexpr char kBacklightRoot[] = "/sys/class/backlight";
  constexpr char kLoginBusName[] = "org.freedesktop.login1";
  constexpr char kLoginSessionObjectPath[] = "/org/freedesktop/login1/session/auto";
  constexpr char kLoginSessionInterface[] = "org.freedesktop.login1.Session";
  constexpr int kDBusTimeoutMs = 1000;
  constexpr int kStepPercent = 5;

  struct Backlight {
    std::string name;
    int current;
    int maximum;
  };

  std::optional<int> readInt(const std::filesystem::path& path) {
    std::ifstream file(path);
    int value = 0;
    if (!(file >> value)) {
      return std::nullopt;
    }
    return value;
  }

  std::vector<Backlight> backlights() {
    std::vector<Backlight> devices;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kBacklightRoot, ec)) {
      const bool directory = entry.is_directory(ec);
      if (ec || !directory) {
        continue;
      }
      const auto current = readInt(entry.path() / "brightness");
      const auto maximum = readInt(entry.path() / "max_brightness");
      if (!current.has_value() || !maximum.has_value() || *maximum <= 0) {
        continue;
      }
      devices.push_back({entry.path().filename().string(), *current, *maximum});
    }
    std::ranges::sort(devices, {}, &Backlight::name);
    return devices;
  }

  int stepFor(int maximum) {
    const int step = (maximum * kStepPercent + 99) / 100;
    return std::max(1, step);
  }

  bool setBrightness(const std::string& name, int value) {
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
      if (error != nullptr) {
        kLog.warn("failed to connect to system D-Bus: {}", error->message);
        g_error_free(error);
      } else {
        kLog.warn("failed to connect to system D-Bus");
      }
      return false;
    }

    GVariant* result = g_dbus_connection_call_sync(
        connection, kLoginBusName, kLoginSessionObjectPath, kLoginSessionInterface, "SetBrightness",
        g_variant_new("(ssu)", "backlight", name.c_str(), static_cast<guint32>(value)), nullptr, G_DBUS_CALL_FLAGS_NONE,
        kDBusTimeoutMs, nullptr, &error
    );
    g_object_unref(connection);
    if (result == nullptr) {
      if (error != nullptr) {
        kLog.warn("failed to set brightness of '{}' to {}: {}", name, value, error->message);
        g_error_free(error);
      } else {
        kLog.warn("failed to set brightness of '{}' to {}", name, value);
      }
      return false;
    }
    g_variant_unref(result);
    return true;
  }

  void adjust(int direction) {
    const auto devices = backlights();
    if (devices.empty()) {
      kLog.debug("no backlight device found");
      return;
    }
    for (const auto& device : devices) {
      const int target = std::clamp(device.current + (direction * stepFor(device.maximum)), 1, device.maximum);
      if (target == device.current) {
        continue;
      }
      if (setBrightness(device.name, target)) {
        kLog.debug("set '{}' brightness to {}/{}", device.name, target, device.maximum);
      }
    }
  }
} // namespace

namespace brightness {
  bool handleKey(std::uint32_t sym) {
    if (KeySymbol::isBrightnessUp(sym)) {
      adjust(1);
      return true;
    }
    if (KeySymbol::isBrightnessDown(sym)) {
      adjust(-1);
      return true;
    }
    return false;
  }
} // namespace brightness
