#include "time/time_format.h"

#include <array>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <print>
#include <string_view>
#include <utility>

namespace {

  bool expect(std::string_view name, std::string_view actual, std::string_view expected) {
    if (actual == expected) {
      return true;
    }
    std::println(stderr, "{}: expected '{}', got '{}'", name, expected, actual);
    return false;
  }

} // namespace

int main() {
  std::setlocale(LC_ALL, "C");
  ::setenv("TZ", "UTC", 1);
  ::tzset();

  constexpr std::int64_t kExampleTime = 1777885507; // 2026-05-04 09:05:07 UTC
  bool passed = true;
  passed =
      expect("bare strftime", formatLocalUnixTime(kExampleTime, "%Y-%m-%d %H:%M:%S"), "2026-05-04 09:05:07") && passed;
  passed = expect("chrono fields", formatLocalUnixTime(kExampleTime, "{:%H:%M} {:%A}"), "09:05 Monday") && passed;
  passed =
      expect("newline escape", formatLocalUnixTime(kExampleTime, "{:%H:%M}\\n{:%F}"), "09:05\n2026-05-04") && passed;
  passed = expect("literal braces", formatLocalUnixTime(kExampleTime, "{{{:%H}}}"), "{09}") && passed;
  passed = expect("unix seconds", formatLocalUnixTime(kExampleTime, "%s"), "1777885507") && passed;
  passed = expect("empty format", formatLocalUnixTime(kExampleTime, ""), "") && passed;

  passed =
      expect("named UTC", formatTimezoneUnixTime(kExampleTime, "%F %T %z %Z", "UTC"), "2026-05-04 09:05:07 +0000 UTC")
      && passed;
  passed = expect(
               "Berlin summer", formatTimezoneUnixTime(kExampleTime, "{:%F} {:%T %z %Z}", "Europe/Berlin"),
               "2026-05-04 11:05:07 +0200 CEST"
           )
      && passed;
  passed =
      expect("quarter hour offset", formatTimezoneUnixTime(kExampleTime, "%H:%M %z", "Asia/Kathmandu"), "14:50 +0545")
      && passed;
  passed = expect("half hour offset", formatTimezoneUnixTime(kExampleTime, "%H:%M %z", "Asia/Kolkata"), "14:35 +0530")
      && passed;
  passed = expect(
               "positive date offset", formatTimezoneUnixTime(kExampleTime, "%F %T %z", "Pacific/Kiritimati"),
               "2026-05-04 23:05:07 +1400"
           )
      && passed;
  passed =
      expect("previous day", formatTimezoneUnixTime(kExampleTime, "%F %T", "Pacific/Honolulu"), "2026-05-03 23:05:07")
      && passed;
  passed = expect("zone epoch", formatTimezoneUnixTime(kExampleTime, "{:%s}", "Europe/Berlin"), "1777885507") && passed;
  passed = expect("literal zone escape", formatTimezoneUnixTime(kExampleTime, "%Z %%Z", "Europe/Berlin"), "CEST %Z")
      && passed;
  passed = expect(
               "named zone newline", formatTimezoneUnixTime(kExampleTime, "{:%H:%M}\\n{:%F}", "Europe/Berlin"),
               "11:05\n2026-05-04"
           )
      && passed;
  passed = expect("named zone empty format", formatTimezoneUnixTime(kExampleTime, "", "UTC"), "") && passed;
  passed = expect("empty zone uses local", formatTimezoneUnixTime(kExampleTime, "%T", ""), "09:05:07") && passed;

  using namespace std::chrono;
  const auto spring = sys_days{2026y / March / 29} + 59min;
  const auto autumn = sys_days{2026y / October / 25} + 59min;
  for (const auto& [instant, expected] : std::array<std::pair<sys_seconds, std::string_view>, 4>{{
           {spring, "01:59 +0100 CET"},
           {spring + 1min, "03:00 +0200 CEST"},
           {autumn, "02:59 +0200 CEST"},
           {autumn + 1min, "02:00 +0100 CET"},
       }}) {
    passed = expect(
                 "DST transition",
                 formatTimezoneUnixTime(instant.time_since_epoch().count(), "%H:%M %z %Z", "Europe/Berlin"), expected
             )
        && passed;
  }
  try {
    (void)formatTimezoneUnixTime(kExampleTime, "%T", "Not/A_Timezone");
    std::println(stderr, "invalid timezone did not throw");
    passed = false;
  } catch (const std::exception&) {
  }
  passed = expect("timezone environment unchanged", std::getenv("TZ"), "UTC") && passed;
  passed = expect("local formatter unchanged", formatLocalUnixTime(kExampleTime, "%T %z"), "09:05:07 +0000") && passed;

  for (const auto format : std::array<std::string_view, 12>{
           "%S", "{:%H:%M:%S}", "%T", "%X", "%EX", "%OS", "%-S", "%3S", "%r", "%c", "%s", "%%%S"
       }) {
    if (!formatShowsSeconds(format)) {
      std::println(stderr, "missing seconds in '{}'", format);
      passed = false;
    }
  }
  for (const auto format : std::array<std::string_view, 11>{
           "", "%H:%M", "%R", "%Y-%m-%d", "%%S", "%%T", "literal", "%", "%-", "%E", "%%%%S"
       }) {
    if (formatShowsSeconds(format)) {
      std::println(stderr, "unexpected seconds in '{}'", format);
      passed = false;
    }
  }
  return passed ? 0 : 1;
}
