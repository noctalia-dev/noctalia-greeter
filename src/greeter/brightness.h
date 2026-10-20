#pragma once

#include <cstdint>

namespace brightness {
  // Consumes the XF86 brightness keys and adjusts the seat backlight through logind.
  bool handleKey(std::uint32_t sym);
} // namespace brightness
