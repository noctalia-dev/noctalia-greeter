#include "time/time_format.h"
#include "time/time_locale.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

  bool expect(std::string_view name, bool condition) {
    if (!condition) {
      std::fprintf(stderr, "%.*s: failed\n", static_cast<int>(name.size()), name.data());
    }
    return condition;
  }

  std::string environmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
  }

  void resetLocale() {
    ::unsetenv("LC_ALL");
    ::unsetenv("LC_TIME");
    ::unsetenv("LANG");
    std::setlocale(LC_ALL, "C");
  }

  void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path);
    file << contents;
    file.close();
    if (!file) {
      std::fprintf(stderr, "could not write locale fixture\n");
      std::exit(EXIT_FAILURE);
    }
  }

} // namespace

int main() {
  char pattern[] = "/tmp/noctalia-time-locale-XXXXXX";
  const char* directory = ::mkdtemp(pattern);
  if (directory == nullptr) {
    std::perror("mkdtemp");
    return EXIT_FAILURE;
  }
  const std::filesystem::path root(directory);
  const auto primary = root / "locale.conf";
  const auto secondary = root / "default-locale";
  const auto initialize = [&] { return initializeTimeLocale(primary.c_str(), secondary.c_str()); };
  bool passed = true;
  ::setenv("TZ", "UTC", 1);
  ::tzset();
  constexpr std::int64_t kExampleTime = 1777885507; // 2026-05-04 09:05:07 UTC

  resetLocale();
  writeFile(primary, "# system locale\nLC_TIME = 'POSIX' # date preference\nexport LANG=\"C\"\n");
  writeFile(secondary, "LC_TIME=C\n");
  passed =
      expect("system LC_TIME precedes LANG and secondary file", initialize() && environmentValue("LC_TIME") == "POSIX")
      && passed;

  resetLocale();
  writeFile(primary, "LANG = \"POSIX\"\n");
  passed = expect("system LANG is the time fallback", initialize() && environmentValue("LC_TIME") == "POSIX") && passed;
  passed = expect("only LC_TIME is filled", environmentValue("LANG").empty()) && passed;

  resetLocale();
  std::filesystem::remove(primary);
  writeFile(secondary, "LANG=POSIX\n");
  passed = expect("Debian locale file fallback", initialize() && environmentValue("LC_TIME") == "POSIX") && passed;

  resetLocale();
  writeFile(primary, "LC_TIME=C\n");
  ::setenv("LANG", "POSIX", 1);
  passed =
      expect("inherited LANG wins over system LC_TIME", initialize() && environmentValue("LC_TIME").empty()) && passed;
  passed = expect("inherited LANG is unchanged", environmentValue("LANG") == "POSIX") && passed;

  resetLocale();
  ::setenv("LANG", "noctalia_INVALID.UTF-8", 1);
  ::setenv("LC_TIME", "POSIX", 1);
  passed = expect("inherited LC_TIME wins over LANG and system", initialize() && environmentValue("LC_TIME") == "POSIX")
      && passed;

  resetLocale();
  ::setenv("LC_ALL", "C", 1);
  ::setenv("LC_TIME", "noctalia_INVALID.UTF-8", 1);
  passed = expect("LC_ALL wins over LC_TIME", initialize() && environmentValue("LC_TIME") == "noctalia_INVALID.UTF-8")
      && passed;

  resetLocale();
  ::setenv("LC_ALL", "", 1);
  ::setenv("LC_TIME", "", 1);
  ::setenv("LANG", "", 1);
  writeFile(primary, "LC_TIME=POSIX\n");
  passed = expect("empty environment values allow fallback", initialize() && environmentValue("LC_TIME") == "POSIX")
      && passed;

  resetLocale();
  writeFile(
      primary, "LC_TIME=\"unterminated\nLC_TIME='C' trailing-junk\nLC_ALL=C\nPATH=/invalid\nLANG=$(touch marker)\n"
  );
  passed =
      expect(
          "malformed and unsupported assignments are ignored", initialize() && environmentValue("LC_TIME") == "POSIX"
      )
      && passed;

  resetLocale();
  writeFile(primary, "LC_TIME=noctalia_INVALID.UTF-8\n");
  passed = expect("unavailable configured locale is reported", !initialize()) && passed;

  resetLocale();
  ::setenv("LANG", "noctalia_INVALID.UTF-8", 1);
  passed = expect(
               "unavailable explicit locale is reported without replacement",
               !initialize() && environmentValue("LC_TIME").empty()
           )
      && passed;

  resetLocale();
  std::filesystem::remove(primary);
  std::filesystem::remove(secondary);
  std::string expectedFallback = "C";
  for (const char* candidate : {"en_US.UTF-8", "C.UTF-8", "C"}) {
    if (std::setlocale(LC_TIME, candidate) != nullptr) {
      expectedFallback = candidate;
      break;
    }
  }
  resetLocale();
  passed = expect(
               "missing system files use an installed default locale",
               initialize() && environmentValue("LC_TIME") == expectedFallback
           )
      && passed;

  for (const char* locale : {"en_GB.UTF-8", "en_US.UTF-8"}) {
    resetLocale();
    if (std::setlocale(LC_TIME, locale) == nullptr) {
      std::fprintf(stderr, "skip date ordering for unavailable locale %s\n", locale);
      continue;
    }
    const auto expected = formatLocalUnixTime(kExampleTime, "%A, %x");
    resetLocale();
    writeFile(primary, std::string("LC_TIME=") + locale + "\n");
    passed = expect(
                 "system fallback preserves locale date ordering",
                 initialize() && formatLocalUnixTime(kExampleTime, "%A, %x") == expected
             )
        && passed;
    passed =
        expect("chrono fields share the selected locale", formatLocalUnixTime(kExampleTime, "{:%A, %x}") == expected)
        && passed;
  }

  resetLocale();
  std::filesystem::remove_all(root);
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
