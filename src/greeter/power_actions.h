#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace power {

  bool powerOff();
  bool reboot();
  bool rebootToFirmwareSetup();
  [[nodiscard]] bool canRebootToFirmwareSetup();
  [[nodiscard]] bool hasSyncedAction(std::string_view action);
  [[nodiscard]] std::optional<std::string> syncedActionLabel(std::string_view action);
  [[nodiscard]] std::optional<std::string> syncedActionGlyph(std::string_view action);

} // namespace power
