#pragma once

#include <cstdint>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace KeySymbol {
  [[nodiscard]] inline bool isEnter(std::uint32_t sym) noexcept {
    return sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter;
  }

  [[nodiscard]] inline bool isBackspace(std::uint32_t sym) noexcept { return sym == XKB_KEY_BackSpace; }

  [[nodiscard]] inline bool isDelete(std::uint32_t sym) noexcept { return sym == XKB_KEY_Delete; }

  [[nodiscard]] inline bool isLeft(std::uint32_t sym) noexcept { return sym == XKB_KEY_Left; }

  [[nodiscard]] inline bool isRight(std::uint32_t sym) noexcept { return sym == XKB_KEY_Right; }

  [[nodiscard]] inline bool isHome(std::uint32_t sym) noexcept { return sym == XKB_KEY_Home; }

  [[nodiscard]] inline bool isEnd(std::uint32_t sym) noexcept { return sym == XKB_KEY_End; }

  [[nodiscard]] inline bool isTab(std::uint32_t sym) noexcept {
    return sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab;
  }

  [[nodiscard]] inline bool isUp(std::uint32_t sym) noexcept { return sym == XKB_KEY_Up; }

  [[nodiscard]] inline bool isDown(std::uint32_t sym) noexcept { return sym == XKB_KEY_Down; }

  [[nodiscard]] inline bool isEscape(std::uint32_t sym) noexcept { return sym == XKB_KEY_Escape; }

  [[nodiscard]] inline bool isSpace(std::uint32_t sym) noexcept {
    return sym == XKB_KEY_space || sym == XKB_KEY_KP_Space;
  }

  [[nodiscard]] inline bool isF3(std::uint32_t sym) noexcept { return sym == XKB_KEY_F3; }

  [[nodiscard]] inline bool isF7(std::uint32_t sym) noexcept { return sym == XKB_KEY_F7; }
} // namespace KeySymbol
