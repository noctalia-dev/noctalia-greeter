#pragma once

#include <optional>

namespace greeter {
  enum class PowerAction { Shutdown, Reboot, Firmware };

  class PowerConfirmation {
  public:
    void request(PowerAction action) { m_pending = action; }
    void cancel() { m_pending.reset(); }
    [[nodiscard]] bool active() const { return m_pending.has_value(); }
    [[nodiscard]] std::optional<PowerAction> accept() {
      const auto action = m_pending;
      m_pending.reset();
      return action;
    }

  private:
    std::optional<PowerAction> m_pending;
  };
} // namespace greeter
