#include "greeter/greeter_sessions.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

  class Fixture {
  public:
    Fixture() {
      std::array<char, 64> pathTemplate{};
      const std::string pattern = "/tmp/noctalia-session-discovery-test.XXXXXX";
      std::copy(pattern.begin(), pattern.end(), pathTemplate.begin());
      char* created = ::mkdtemp(pathTemplate.data());
      if (created == nullptr) {
        throw std::runtime_error(std::string("mkdtemp failed: ") + std::strerror(errno));
      }
      root = created;

      writeDesktopEntry(root / "wayland-sessions" / "foo.desktop", "Foo", "/usr/bin/foo");
      writeDesktopEntry(root / "wayland-sessions" / "shared.desktop", "Shared", "/usr/bin/shared-wayland");
      writeDesktopEntry(root / "xsessions" / "bar.desktop", "Bar", "/usr/bin/bar");
      writeDesktopEntry(root / "xsessions" / "shared.desktop", "Shared", "/usr/bin/shared-x11");
      writeDesktopEntry(root / "xsessions" / "hidden.desktop", "Hidden", "/usr/bin/hidden", /*noDisplay=*/true);
      writeDesktopEntry(
          root / "wayland-sessions" / "actions.desktop", "Actions", "/usr/bin/actions", false,
          "[Desktop Action Other]\nName=Other\nExec=/usr/bin/other\nNoDisplay=true\nHidden=true\nDesktopNames=Other\n"
      );
      std::ofstream(root / "wayland-sessions" / "prelude.desktop")
          << "Name=Outside\nExec=/usr/bin/outside\nHidden=true\n"
             "[Desktop Action Before]\nName=Before\nExec=/usr/bin/before\nNoDisplay=true\n"
             "[Desktop Entry]\nName=Prelude\nExec=/usr/bin/prelude\nDesktopNames=Prelude\n"
             "[Desktop Action After]\nDesktopNames=Other\n";
      std::ofstream(root / "wayland-sessions" / "action-only.desktop")
          << "[Desktop Action Only]\nName=Action Only\nExec=/usr/bin/action-only\n";
      std::ofstream(root / "wayland-sessions" / "hidden-main.desktop")
          << "[Desktop Entry]\nName=Hidden Main\nExec=/usr/bin/hidden-main\nHidden=true\n"
             "[Desktop Action Other]\nHidden=false\n";

      previousXdgDataDirs = std::getenv("XDG_DATA_DIRS") != nullptr ? std::getenv("XDG_DATA_DIRS") : "";
      hadPreviousXdgDataDirs = std::getenv("XDG_DATA_DIRS") != nullptr;
      ::setenv("XDG_DATA_DIRS", root.c_str(), 1);
    }

    ~Fixture() {
      if (hadPreviousXdgDataDirs) {
        ::setenv("XDG_DATA_DIRS", previousXdgDataDirs.c_str(), 1);
      } else {
        ::unsetenv("XDG_DATA_DIRS");
      }
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

  private:
    static void writeDesktopEntry(
        const std::filesystem::path& path, const std::string& name, const std::string& exec,
        const bool noDisplay = false, const std::string& trailer = {}
    ) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream out(path);
      out << "[Desktop Entry]\n";
      out << "Name=" << name << "\n";
      out << "Exec=" << exec << "\n";
      if (noDisplay) {
        out << "NoDisplay=true\n";
      }
      out << trailer;
    }

    std::filesystem::path root;
    std::string previousXdgDataDirs;
    bool hadPreviousXdgDataDirs = false;
  };

  void expect(const bool condition, const std::string& message) {
    if (!condition) {
      throw std::runtime_error("assertion failed: " + message);
    }
  }

  void expectArgv(
      const std::vector<std::string>& actual, const std::vector<std::string>& expected, const std::string& message
  ) {
    expect(actual == expected, message);
  }

  void testSessionArgv() {
    const greeter::SessionOption wayland{
        .name = "Niri", .command = "niri", .desktopNames = "niri", .sessionType = "wayland"
    };
    expectArgv(greeter::sessionArgv(wayland), {"niri"}, "wayland session argv is unwrapped");

    const greeter::SessionOption multiToken{
        .name = "GNOME",
        .command = "dbus-run-session gnome-session",
        .desktopNames = "GNOME",
        .sessionType = "wayland",
    };
    expectArgv(
        greeter::sessionArgv(multiToken), {"dbus-run-session", "gnome-session"},
        "multi-token wayland Exec= splits correctly"
    );

    const greeter::SessionOption x11{
        .name = "Bspwm", .command = "/usr/bin/bspwm", .desktopNames = "bspwm", .sessionType = "x11"
    };
    expectArgv(
        greeter::sessionArgv(x11), {"noctalia-greeter-xsession", "/usr/bin/bspwm"},
        "x11 session argv is wrapped in noctalia-greeter-xsession"
    );

    const greeter::SessionOption tty{.name = "Shell", .command = "/bin/sh", .desktopNames = {}, .sessionType = "tty"};
    expectArgv(greeter::sessionArgv(tty), {"/bin/sh"}, "tty fallback session argv is unwrapped");

    const greeter::SessionOption empty{.name = "Empty", .command = "  ", .desktopNames = {}, .sessionType = "wayland"};
    expect(greeter::sessionArgv(empty).empty(), "whitespace-only Exec= yields empty argv");
  }

} // namespace

int main() {
  const Fixture fixture;

  const std::vector<greeter::SessionOption> sessions = greeter::discoverSessions();

  const auto foo = greeter::findSessionIndex(sessions, "Foo");
  expect(foo.has_value(), "Foo session discovered");
  expect(sessions[*foo].sessionType == "wayland", "Foo session type is wayland");
  expect(sessions[*foo].command == "/usr/bin/foo", "Foo command matches Exec=");

  const auto bar = greeter::findSessionIndex(sessions, "Bar");
  expect(bar.has_value(), "Bar session discovered");
  expect(sessions[*bar].sessionType == "x11", "Bar session type is x11");
  expect(sessions[*bar].command == "/usr/bin/bar", "Bar command matches Exec=");

  expect(!greeter::findSessionIndex(sessions, "Hidden").has_value(), "NoDisplay=true entry excluded");

  const auto actions = greeter::findSessionIndex(sessions, "Actions");
  expect(actions.has_value(), "Desktop Action keys do not override [Desktop Entry]");
  expect(sessions[*actions].command == "/usr/bin/actions", "Desktop Action Exec= ignored");
  expect(sessions[*actions].desktopNames.empty(), "Desktop Action DesktopNames= ignored");
  const auto prelude = greeter::findSessionIndex(sessions, "Prelude");
  expect(prelude.has_value(), "keys before [Desktop Entry] do not hide main session");
  expect(
      sessions[*prelude].command == "/usr/bin/prelude" && sessions[*prelude].desktopNames == "Prelude",
      "only main group command and desktop environment used"
  );
  expect(!greeter::findSessionIndex(sessions, "Action Only"), "action-only file is not a session");
  expect(!greeter::findSessionIndex(sessions, "Hidden Main"), "action group cannot unhide main session");

  const auto shared = greeter::findSessionIndex(sessions, "Shared");
  expect(shared.has_value(), "Shared session discovered");
  expect(sessions[*shared].sessionType == "wayland", "Shared name collision keeps wayland-sessions entry");
  expect(sessions[*shared].command == "/usr/bin/shared-wayland", "Shared command keeps wayland-sessions Exec=");

  testSessionArgv();

  return 0;
}
