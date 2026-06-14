#pragma once

namespace power {

  bool powerOff();
  bool reboot();
  bool rebootToFirmwareSetup();
  [[nodiscard]] bool canRebootToFirmwareSetup();

} // namespace power
