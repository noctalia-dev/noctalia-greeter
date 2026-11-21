#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Formats local wall-clock time with the same bare strftime and C++ chrono
// field syntax used by Noctalia (for example "%H:%M" and "{:%H:%M}").
[[nodiscard]] std::string formatLocalTime(std::string_view format);

// Timestamp overload used by deterministic callers and tests.
[[nodiscard]] std::string formatLocalUnixTime(std::int64_t unixSeconds, std::string_view format);

// Empty timezone selects local time; invalid nonempty names throw rather than substitute local time.
[[nodiscard]] std::string
formatTimezoneUnixTime(std::int64_t unixSeconds, std::string_view format, std::string_view timezoneName);

// Second-bearing strftime/chrono directives, including flags and E/O modifiers; %% stays literal.
[[nodiscard]] bool formatShowsSeconds(std::string_view format) noexcept;
