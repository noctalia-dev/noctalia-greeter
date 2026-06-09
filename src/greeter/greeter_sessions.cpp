#include "greeter/greeter_sessions.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string sanitizeDesktopExec(const std::string &exec) {
  std::istringstream stream(exec);
  std::string token;
  std::string out;
  while (stream >> token) {
    if (!token.empty() && token[0] == '%') {
      continue;
    }
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += token;
  }
  return trim(out);
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string sessionNameKey(std::string_view name) {
  std::string key;
  key.reserve(name.size());
  for (const char ch : name) {
    key.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return key;
}

void appendSearchDirectory(std::vector<std::filesystem::path> &dirs,
                           std::unordered_set<std::string> &seen,
                           const std::filesystem::path &dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) {
    return;
  }

  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(dir, ec);
  const std::string key = ec ? dir.string() : canonical.string();
  if (!seen.insert(key).second) {
    return;
  }
  dirs.push_back(dir);
}

[[nodiscard]] std::vector<std::filesystem::path> sessionSearchDirectories() {
  std::vector<std::filesystem::path> dirs;
  std::unordered_set<std::string> seen;
  seen.reserve(8);

  appendSearchDirectory(dirs, seen, "/usr/local/share/wayland-sessions");
  appendSearchDirectory(dirs, seen, "/usr/share/wayland-sessions");
  appendSearchDirectory(dirs, seen,
                        "/run/current-system/sw/share/wayland-sessions");

  const char *xdgDataDirs = std::getenv("XDG_DATA_DIRS");
  const std::string_view xdgValue =
      xdgDataDirs != nullptr ? std::string_view(xdgDataDirs)
                             : std::string_view("/usr/local/share:/usr/share");

  std::size_t start = 0;
  while (start <= xdgValue.size()) {
    const std::size_t end = xdgValue.find(':', start);
    const std::string base =
        trim(std::string(xdgValue.substr(start, end - start)));
    if (!base.empty()) {
      appendSearchDirectory(dirs, seen,
                            std::filesystem::path(base) / "wayland-sessions");
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  return dirs;
}

void discoverSessionsInDirectory(const std::filesystem::path &dir,
                                 std::vector<greeter::SessionOption> &sessions,
                                 std::unordered_set<std::string> &seenNames) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec || entry.is_directory()) {
      continue;
    }
    if (entry.path().extension() != ".desktop") {
      continue;
    }

    std::ifstream in(entry.path());
    if (!in.is_open()) {
      continue;
    }

    std::string line;
    std::string name;
    std::string exec;
    while (std::getline(in, line)) {
      if (line.rfind("Name=", 0) == 0) {
        name = trim(line.substr(5));
      } else if (line.rfind("Exec=", 0) == 0) {
        exec = sanitizeDesktopExec(line.substr(5));
      }
    }

    if (name.empty() || exec.empty()) {
      continue;
    }
    if (!seenNames.insert(sessionNameKey(name)).second) {
      continue;
    }
    sessions.push_back(greeter::SessionOption{.name = name, .command = exec});
  }
}

} // namespace

namespace greeter {

std::vector<SessionOption> discoverSessions() {
  std::vector<SessionOption> sessions;
  std::unordered_set<std::string> seenNames;

  for (const auto &dir : sessionSearchDirectories()) {
    discoverSessionsInDirectory(dir, sessions, seenNames);
  }

  if (sessions.empty()) {
    sessions.push_back(SessionOption{.name = "Shell", .command = "/bin/sh"});
  }
  return sessions;
}

std::optional<std::size_t>
findSessionIndex(const std::vector<SessionOption> &sessions,
                 std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < sessions.size(); ++i) {
    if (equalsIgnoreCase(sessions[i].name, name)) {
      return i;
    }
  }
  return std::nullopt;
}

} // namespace greeter
