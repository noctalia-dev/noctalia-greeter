#include "time/time_format.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <locale>
#include <optional>
#include <string>
#include <string_view>

namespace {

  [[nodiscard]] std::string normalizeFormatEscapes(std::string_view format) {
    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size(); ++i) {
      if (format[i] == '\\' && i + 1 < format.size() && format[i + 1] == 'n') {
        out.push_back('\n');
        ++i;
      } else {
        out.push_back(format[i]);
      }
    }
    return out;
  }

  [[nodiscard]] bool shouldUseStrftimeCompat(std::string_view format) {
    const auto has = [format](const std::string_view token) { return format.contains(token); };
    return has("%-") || (has("%") && (!has("{") || has("{:")));
  }

  // musl resolves %Z from its own timezone state and ignores tm_zone.
  [[nodiscard]] std::string substituteTimezoneAbbreviation(std::string_view format, const char* abbreviation) {
    if (!format.contains("%Z") || abbreviation == nullptr) {
      return std::string(format);
    }

    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] != '%' || i + 1 >= format.size()) {
        out.push_back(format[i++]);
      } else if (format[i + 1] == '%') {
        out.append("%%");
        i += 2;
      } else if (format[i + 1] == 'Z') {
        for (const char* c = abbreviation; *c != '\0'; ++c) {
          if (*c == '%') {
            out.push_back('%');
          }
          out.push_back(*c);
        }
        i += 2;
      } else {
        out.push_back(format[i++]);
      }
    }
    return out;
  }

  [[nodiscard]] std::string formatStrftimeRaw(std::string_view format, const std::tm& local) {
    const std::string spec = substituteTimezoneAbbreviation(format, local.tm_zone);
    std::size_t size = std::max<std::size_t>(64, spec.size() * 4 + 16);
    for (int attempt = 0; attempt < 6; ++attempt) {
      std::string buffer(size, '\0');
      const std::size_t written = std::strftime(buffer.data(), buffer.size(), spec.c_str(), &local);
      if (written > 0 || spec.empty()) {
        buffer.resize(written);
        return buffer;
      }
      size *= 2;
    }
    return {};
  }

  [[nodiscard]] std::string
  formatStrftime(std::string_view format, const std::tm& local, const std::optional<std::int64_t> unixSeconds) {
    if (!unixSeconds.has_value() || !format.contains("%s")) {
      return formatStrftimeRaw(format, local);
    }

    std::string out;
    std::string chunk;
    out.reserve(format.size() + 16);
    chunk.reserve(format.size());
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] != '%' || i + 1 >= format.size()) {
        chunk.push_back(format[i++]);
      } else if (format[i + 1] == '%') {
        chunk.append("%%");
        i += 2;
      } else if (format[i + 1] == 's') {
        out += formatStrftimeRaw(chunk, local);
        chunk.clear();
        out += std::to_string(*unixSeconds);
        i += 2;
      } else {
        chunk.push_back(format[i++]);
      }
    }
    out += formatStrftimeRaw(chunk, local);
    return out;
  }

  [[nodiscard]] std::optional<std::string>
  formatStrftimeCompat(std::string_view format, const std::tm& local, const std::optional<std::int64_t> unixSeconds) {
    if (!shouldUseStrftimeCompat(format)) {
      return std::nullopt;
    }
    if (!format.contains('{')) {
      return formatStrftime(format, local, unixSeconds);
    }

    std::string out;
    out.reserve(format.size());
    bool formattedField = false;
    for (std::size_t i = 0; i < format.size();) {
      if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{') {
        out.push_back('{');
        i += 2;
        continue;
      }
      if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}') {
        out.push_back('}');
        i += 2;
        continue;
      }
      if (format[i] != '{') {
        out.push_back(format[i++]);
        continue;
      }

      const std::size_t end = format.find('}', i + 1);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      const std::string_view field = format.substr(i + 1, end - i - 1);
      const std::size_t colon = field.find(':');
      if (colon == std::string_view::npos) {
        return std::nullopt;
      }
      std::string_view spec = field.substr(colon + 1);
      const std::size_t firstPercent = spec.find('%');
      if (firstPercent == std::string_view::npos) {
        return std::nullopt;
      }
      spec.remove_prefix(firstPercent);
      out += formatStrftime(spec, local, unixSeconds);
      formattedField = true;
      i = end + 1;
    }
    return formattedField ? std::make_optional(std::move(out)) : std::nullopt;
  }

  [[nodiscard]] std::string formatWallTime(std::int64_t unixSeconds, std::string_view format, const std::tm& local) {
    if (format.empty())
      return {};
    std::string normalized = normalizeFormatEscapes(format);
    if (const auto compat = formatStrftimeCompat(normalized, local, unixSeconds))
      return *compat;
    const auto localTime = std::chrono::local_seconds{std::chrono::seconds{unixSeconds + local.tm_gmtoff}};
    try {
      return std::vformat(std::locale(""), normalized, std::make_format_args(localTime));
    } catch (...) {
      return normalized;
    }
  }

} // namespace

std::string formatLocalUnixTime(const std::int64_t unixSeconds, const std::string_view format) {
  const auto timePoint = std::chrono::sys_seconds{std::chrono::seconds{unixSeconds}};
  const std::time_t raw = std::chrono::system_clock::to_time_t(timePoint);
  std::tm local{};
  localtime_r(&raw, &local);
  return formatWallTime(unixSeconds, format, local);
}

std::string formatLocalTime(const std::string_view format) {
  using namespace std::chrono;
  const auto now = floor<seconds>(system_clock::now());
  return formatLocalUnixTime(duration_cast<seconds>(now.time_since_epoch()).count(), format);
}

std::string formatTimezoneUnixTime(std::int64_t unixSeconds, std::string_view format, std::string_view timezoneName) {
  if (timezoneName.empty())
    return formatLocalUnixTime(unixSeconds, format);
  using namespace std::chrono;
  const auto* zone = locate_zone(timezoneName);
  const auto timestamp = sys_seconds{seconds{unixSeconds}};
  const auto info = zone->get_info(timestamp);
  const auto local = zone->to_local(timestamp);
  const auto localDays = floor<days>(local);
  const year_month_day date{localDays};
  const hh_mm_ss time{local - localDays};
  std::tm broken{};
  broken.tm_year = static_cast<int>(date.year()) - 1900;
  broken.tm_mon = static_cast<int>(static_cast<unsigned>(date.month())) - 1;
  broken.tm_mday = static_cast<int>(static_cast<unsigned>(date.day()));
  broken.tm_hour = static_cast<int>(time.hours().count());
  broken.tm_min = static_cast<int>(time.minutes().count());
  broken.tm_sec = static_cast<int>(time.seconds().count());
  broken.tm_wday = static_cast<int>(weekday(localDays).c_encoding());
  broken.tm_yday = static_cast<int>((localDays - local_days{date.year() / January / 1}).count());
  broken.tm_isdst = info.save != minutes::zero();
  broken.tm_gmtoff = static_cast<long>(info.offset.count());
  broken.tm_zone = info.abbrev.c_str();
  return formatWallTime(unixSeconds, format, broken);
}

bool formatShowsSeconds(std::string_view format) noexcept {
  for (std::size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%')
      continue;
    std::size_t j = i + 1;
    if (j < format.size() && format[j] == '%') {
      i = j;
      continue;
    }
    while (j < format.size()
           && (std::string_view{"-_0^#"}.contains(format[j]) || (format[j] >= '0' && format[j] <= '9')))
      ++j;
    if (j < format.size() && (format[j] == 'E' || format[j] == 'O'))
      ++j;
    if (j < format.size() && std::string_view{"STXrcs"}.contains(format[j]))
      return true;
    i = j - 1;
  }
  return false;
}
