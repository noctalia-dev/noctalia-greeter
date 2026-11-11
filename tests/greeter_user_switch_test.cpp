#include "greeter/greeter.h"
#include "greeter/greeter_window.h"
#include "wayland/wayland_client.h"
#include "wayland/wayland_seat.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
  using Json = nlohmann::json;
  using namespace std::chrono_literals;

  std::vector<GreeterWindow*> windows;
  WaylandSeat* seat = nullptr;
  std::atomic<unsigned> pollCount{0};

  void require(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  // Run production socket dispatch and rendering. A sync reply wakes poll when
  // the bounded test step ends; input and auth handling stay on the main thread.
  void pump(Greeter& greeter, WaylandClient& client) {
    std::atomic<bool> stop{false};
    const auto before = pollCount.load();
    std::jthread wake([&](std::stop_token token) {
      // Let the event loop enter poll at least once, even on a busy builder.
      // The synthetic greetd reply is already readable before pump is called.
      while (pollCount.load() == before) {
        if (token.stop_requested()) {
          return;
        }
        std::this_thread::sleep_for(1ms);
      }
      std::this_thread::sleep_for(20ms);
      if (token.stop_requested()) {
        return;
      }
      stop = true;
      auto* callback = wl_display_sync(client.display());
      static const wl_callback_listener listener = {
          .done = [](void*, wl_callback* completed, std::uint32_t) { wl_callback_destroy(completed); },
      };
      wl_callback_add_listener(callback, &listener, nullptr);
      (void)client.flush();
    });
    const int result = greeter.run(client, stop);
    wake.request_stop();
    require(result == 0, "greeter event loop failed");
  }

  class FakeGreetd {
  public:
    FakeGreetd() {
      char pattern[] = "/tmp/noctalia-user-switch-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      require(created != nullptr, "mkdtemp failed");
      directory = created;
      std::ofstream(directory / "greeter.toml") << R"toml(
[appearance]
hide_logo = true
[clock]
enabled = false
[user]
default = "user-switch-test"
[auth]
allow_empty_password = true
)toml";
      require(::setenv("NOCTALIA_GREETER_STATE_DIR", directory.c_str(), 1) == 0, "setenv failed");
      require(::setenv("NOCTALIA_GREETER_DUMMY_USERS", "2", 1) == 0, "setenv failed");
      const std::string path = (directory / "greetd.sock").string();
      require(::setenv("GREETD_SOCK", path.c_str(), 1) == 0, "setenv failed");
      listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
      require(listener >= 0, "socket failed");
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      require(path.size() < sizeof(address.sun_path), "socket path too long");
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      require(
          ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0
              && ::listen(listener, 1) == 0,
          "bind/listen failed"
      );
    }

    ~FakeGreetd() {
      if (peer >= 0) {
        ::close(peer);
      }
      if (listener >= 0) {
        ::close(listener);
      }
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }

    void acceptPeer() {
      peer = ::accept(listener, nullptr, nullptr);
      require(peer >= 0, "accept failed");
    }

    Json request(std::string_view type) {
      std::uint32_t length = 0;
      readAll(&length, sizeof(length));
      require(length <= 65536, "oversized IPC request");
      std::string payload(length, '\0');
      readAll(payload.data(), payload.size());
      const auto value = Json::parse(payload);
      std::printf("IPC: %s\n", payload.c_str());
      if (value.at("type") != type) {
        throw std::runtime_error("expected " + std::string(type) + ", received " + payload);
      }
      return value;
    }

    void noRequest() const {
      pollfd ready{peer, POLLIN, 0};
      require(::poll(&ready, 1, 20) == 0, "unexpected request or disconnection while changing users");
    }

    void disconnected() const {
      pollfd ready{peer, POLLIN, 0};
      char byte;
      require(
          ::poll(&ready, 1, 1000) > 0 && ::recv(peer, &byte, 1, MSG_PEEK) == 0,
          "failed cancellation did not close the authentication connection"
      );
    }

    void reply(Greeter& greeter, WaylandClient& client, const Json& value) {
      const std::string payload = value.dump();
      const auto length = static_cast<std::uint32_t>(payload.size());
      std::string frame(reinterpret_cast<const char*>(&length), sizeof(length));
      frame += payload;
      require(
          ::send(peer, frame.data(), frame.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(frame.size()),
          "could not send IPC reply"
      );
      pump(greeter, client);
    }

    void prompt(Greeter& greeter, WaylandClient& client, std::string_view type) {
      reply(greeter, client, {{"type", "auth_message"}, {"auth_message_type", type}, {"auth_message", "Test prompt"}});
    }

  private:
    void readAll(void* destination, std::size_t length) const {
      auto* cursor = static_cast<char*>(destination);
      while (length > 0) {
        pollfd ready{peer, POLLIN, 0};
        require(::poll(&ready, 1, 1000) > 0, "timed out waiting for an IPC request");
        const ssize_t count = ::read(peer, cursor, length);
        require(count > 0, "incomplete IPC request");
        cursor += count;
        length -= static_cast<std::size_t>(count);
      }
    }

    std::filesystem::path directory;
    int listener = -1;
    int peer = -1;
  };

  void focusOutput(std::size_t index) {
    require(seat != nullptr && index < windows.size(), "test output or seat missing");
    WaylandSeat::handlePointerEnter(seat, nullptr, 1, windows[index]->wlSurface(), 0, 0);
    WaylandSeat::handlePointerFrame(seat, nullptr);
  }

  void key(Greeter& greeter, std::uint32_t symbol) { greeter.onKeyboardEvent(symbol, 0, 0, true, false); }

  void type(Greeter& greeter, std::string_view text) {
    for (const unsigned char ch : text) {
      greeter.onKeyboardEvent(ch, ch, 0, true, false);
    }
  }

  void submit(Greeter& greeter, std::string_view text = {}) {
    type(greeter, text);
    key(greeter, XKB_KEY_Return);
  }

  void selectUser(Greeter& greeter, std::size_t output, std::string_view username) {
    focusOutput(output);
    key(greeter, XKB_KEY_Escape);
    key(greeter, XKB_KEY_Down);
    submit(greeter, username);
  }

  void finish(FakeGreetd& server, Greeter& greeter, WaylandClient& client) {
    server.reply(greeter, client, {{"type", "success"}});
    server.request("start_session");
    server.reply(greeter, client, {{"type", "success"}});
    const auto before = pollCount.load();
    pump(greeter, client);
    require(pollCount.load() == before, "greeter kept running after session handoff");
    server.noRequest();
  }

  void testSwitch(std::string_view stage) {
    FakeGreetd server;
    WaylandClient client;
    require(client.connect(), "Wayland connection failed");
    Greeter greeter;
    require(greeter.initialize(client), "greeter initialization failed");
    for (int i = 0; i < 3; ++i) {
      require(wl_display_roundtrip(client.display()) >= 0, "Wayland roundtrip failed");
    }
    require(windows.size() == 2, "test requires two rendered outputs");
    server.acceptPeer();
    focusOutput(0);
    if (stage == "idle-switch") {
      type(greeter, "must-not-leak");
      selectUser(greeter, 1, "dummy-user-01");
      focusOutput(0);
      submit(greeter, "new-account-token");
      require(server.request("create_session").at("username") == "dummy-user-01", "idle user selection did not mirror");
      server.prompt(greeter, client, "secret");
      require(
          server.request("post_auth_message_response").at("response") == "new-account-token",
          "unsent password survived a change of account"
      );
      finish(server, greeter, client);
      return;
    }
    submit(greeter, "old-account-token");
    require(server.request("create_session").at("username") == "user-switch-test", "wrong initial account");

    if (stage != "create-pending" && stage != "create-info") {
      server.prompt(greeter, client, "secret");
      require(
          server.request("post_auth_message_response").at("response") == "old-account-token", "wrong initial credential"
      );
    }
    if (stage == "prompt-waiting" || stage == "cancel-reselect" || stage == "reselect-before-ack") {
      server.prompt(greeter, client, "secret");
      server.noRequest();
    }
    if (stage == "handoff") {
      server.reply(greeter, client, {{"type", "success"}});
      server.request("start_session");
    }
    if (stage == "error-cancel-pending") {
      server.reply(
          greeter, client, {{"type", "error"}, {"error_type", "auth_error"}, {"description", "Old account failure"}}
      );
      server.request("cancel_session");
    }

    selectUser(greeter, (stage == "same-output" || stage == "reselect-before-ack") ? 0 : 1, "dummy-user-01");
    if (stage == "handoff") {
      server.noRequest();
      server.reply(greeter, client, {{"type", "success"}});
      const auto before = pollCount.load();
      pump(greeter, client);
      require(pollCount.load() == before, "account change swallowed the terminal handoff");
      return;
    }
    if (stage != "prompt-waiting" && stage != "cancel-reselect" && stage != "error-cancel-pending"
        && stage != "reselect-before-ack") {
      server.noRequest();
      focusOutput(0);
      submit(greeter, "queued-account-token");
      server.noRequest();
      if (stage == "create-pending") {
        server.prompt(greeter, client, "secret");
      } else if (stage == "create-info" || stage == "old-info") {
        server.prompt(greeter, client, "info");
      } else if (stage == "old-error") {
        server.reply(
            greeter, client, {{"type", "error"}, {"error_type", "auth_error"}, {"description", "Old account failure"}}
        );
      } else {
        server.reply(greeter, client, {{"type", "success"}});
      }
    }
    if (stage != "error-cancel-pending") {
      server.request("cancel_session");
    }
    if (stage == "cancel-reselect") {
      selectUser(greeter, 0, "dummy-user-02");
      server.noRequest();
    }
    if (stage == "reselect-before-ack") {
      // Reselecting while the cancel ack is outstanding keeps authentication
      // ownership here: the other output cannot begin authentication and the
      // shared client must not disconnect.
      focusOutput(1);
      submit(greeter, "must-not-begin-auth");
      server.noRequest();
      server.reply(greeter, client, {{"type", "success"}});
      server.noRequest();
      focusOutput(0);
      submit(greeter, "fresh-account-token");
      const auto create = server.request("create_session");
      require(create.at("username") == "dummy-user-01", "reselection before the cancel ack lost the account");
      server.prompt(greeter, client, "secret");
      require(server.request("post_auth_message_response").at("response") == "fresh-account-token",
          "the fresh credential did not reach the restarted conversation");
      finish(server, greeter, client);
      return;
    }
    // Stages that skipped the pre-drain submit arm their credential here;
    // deferred stages already hold it from before the stale reply. The other
    // output cannot begin authentication while the owner owns the connection.
    if (stage == "prompt-waiting" || stage == "cancel-reselect" || stage == "error-cancel-pending") {
      focusOutput(0);
      submit(greeter, "queued-account-token");
      server.noRequest();
    }
    focusOutput(1);
    submit(greeter, "must-not-cross-surface");
    server.noRequest();
    if (stage == "cancel-error") {
      server.reply(
          greeter, client, {{"type", "error"}, {"error_type", "error"}, {"description", "Cancellation failed"}}
      );
      server.disconnected();
      return;
    }
    server.reply(greeter, client, {{"type", "success"}});

    const auto create = server.request("create_session");
    require(
        create.at("username") == (stage == "cancel-reselect" ? "dummy-user-02" : "dummy-user-01"),
        "new login used the previous account"
    );
    server.prompt(greeter, client, "secret");
    require(
        server.request("post_auth_message_response").at("response") == "queued-account-token",
        "the submission queued during cancellation did not reach the new login"
    );
    finish(server, greeter, client);
  }
} // namespace

// Observe production Wayland listeners to inject input through the real seat;
// keep application state private and forward all listener registrations.
extern "C" int __real_wl_proxy_add_listener(wl_proxy*, void (**)(void), void*);
extern "C" int __wrap_wl_proxy_add_listener(wl_proxy* proxy, void (**implementation)(void), void* data) {
  const char* name = wl_proxy_get_class(proxy);
  if (std::strcmp(name, "xdg_surface") == 0) {
    windows.push_back(static_cast<GreeterWindow*>(data));
  } else if (std::strcmp(name, "wl_seat") == 0) {
    seat = static_cast<WaylandSeat*>(data);
  }
  return __real_wl_proxy_add_listener(proxy, implementation, data);
}

extern "C" int __real_poll(pollfd*, nfds_t, int);
extern "C" int __wrap_poll(pollfd* descriptors, nfds_t count, int timeout) {
  ++pollCount;
  return __real_poll(descriptors, count, timeout);
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "test stage is required");
    testSwitch(argv[1]);
    std::puts("user switch: cancellation and account isolation passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "user switch: %s\n", error.what());
    return 1;
  }
}
