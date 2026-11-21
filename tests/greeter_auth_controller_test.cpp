#include "greeter/greeter_auth_controller.h"

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
#include <unistd.h>

namespace {
  using Json = nlohmann::json;

  void require(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  // Exercise the controller with the real transport and no Wayland connection,
  // renderer, input field, or scene lifetime to keep it alive.
  class Conversation {
  public:
    explicit Conversation(bool allowEmptyPassword = true) : auth(model, client) {
      char pattern[] = "/tmp/noctalia-auth-controller-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      require(created != nullptr, "mkdtemp failed");
      directory = created;
      require(::setenv("NOCTALIA_GREETER_STATE_DIR", directory.c_str(), 1) == 0, "setenv failed");
      require(::setenv("NOCTALIA_GREETER_DUMMY_USERS", "2", 1) == 0, "setenv failed");
      std::ofstream(directory / "greeter.toml")
          << "[user]\ndefault = 'controller-test'\n[auth]\nallow_empty_password = "
          << (allowEmptyPassword ? "true" : "false")
          << '\n';
      model.initialize();

      const auto path = (directory / "greetd.sock").string();
      listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
      require(listener >= 0, "socket failed");
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      require(path.size() < sizeof(address.sun_path), "socket path too long");
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      require(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "bind failed");
      require(::listen(listener, 1) == 0 && client.connect(path), "listen/connect failed");
      peer = ::accept(listener, nullptr, nullptr);
      require(peer >= 0, "accept failed");
    }

    ~Conversation() {
      if (peer >= 0) {
        ::close(peer);
      }
      if (listener >= 0) {
        ::close(listener);
      }
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }

    Json request(std::string_view type) {
      auto exact = [this](void* data, std::size_t size) {
        auto* cursor = static_cast<char*>(data);
        while (size > 0) {
          pollfd ready{peer, POLLIN, 0};
          require(::poll(&ready, 1, 1000) > 0, "timed out waiting for controller request");
          const auto count = ::read(peer, cursor, size);
          require(count > 0, "incomplete controller request");
          cursor += count;
          size -= static_cast<std::size_t>(count);
        }
      };
      std::uint32_t size = 0;
      exact(&size, sizeof(size));
      require(size <= 65536, "oversized request");
      std::string payload(size, '\0');
      exact(payload.data(), payload.size());
      auto value = Json::parse(payload);
      require(value.at("type") == type, "unexpected controller request type");
      return value;
    }

    void reply(const Json& value) {
      const std::string payload = value.dump();
      const auto size = static_cast<std::uint32_t>(payload.size());
      std::string frame(reinterpret_cast<const char*>(&size), sizeof(size));
      frame += payload;
      require(
          ::send(peer, frame.data(), frame.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(frame.size()), "send failed"
      );
      auth.onGreetdReadable();
    }

    void prompt(std::string_view type) {
      reply({{"type", "auth_message"}, {"auth_message_type", type}, {"auth_message", "Sample PAM message"}});
    }

    void noRequest() const {
      pollfd ready{peer, POLLIN, 0};
      require(::poll(&ready, 1, 20) == 0, "unexpected controller request");
    }

    void disconnected() const {
      pollfd ready{peer, POLLIN, 0};
      char byte;
      require(
          ::poll(&ready, 1, 1000) > 0 && ::recv(peer, &byte, 1, MSG_PEEK) == 0,
          "controller must close transport without sending another request"
      );
    }

    void closePeer() {
      ::close(peer);
      peer = -1;
    }

    GreetdClient client;
    GreeterModel model;
    GreeterAuthController auth;

  private:
    std::filesystem::path directory;
    int listener = -1;
    int peer = -1;
  };

  void testPrompts() {
    Conversation conversation;
    auto& auth = conversation.auth;
    const auto& state = conversation.model.state();
    auth.submit(1, "");
    require(conversation.request("create_session").at("username") == "controller-test", "wrong account");
    conversation.prompt("info");
    require(!conversation.request("post_auth_message_response").contains("response"), "info received a credential");
    require(state.promptType == GreetdAuthMessageType::Info && !state.statusIsError, "info state lost");
    conversation.prompt("secret");
    require(conversation.request("post_auth_message_response").at("response") == "", "queued empty response lost");
    conversation.prompt("secret");
    require(state.waitingForInput && state.promptType == GreetdAuthMessageType::Secret, "secret prompt lost");
    require(!auth.inputBlocked(1) && auth.inputBlocked(2), "prompt ownership lost");
    auth.submit(2, "other-output");
    conversation.noRequest();
    auth.submit(1, "second-prompt");
    require(conversation.request("post_auth_message_response").at("response") == "second-prompt", "wrong response");
    conversation.prompt("visible");
    require(state.waitingForInput && state.promptType == GreetdAuthMessageType::Visible, "visible prompt type lost");
    require(state.prompt == "Sample PAM message", "visible prompt text was not preserved separately");
    auth.submit(1, "visible-answer");
    require(
        conversation.request("post_auth_message_response").at("response") == "visible-answer", "visible input lost"
    );
    conversation.prompt("error");
    require(!conversation.request("post_auth_message_response").contains("response"), "error received a credential");
    require(state.statusIsError && state.promptType == GreetdAuthMessageType::Error, "PAM error state lost");
    conversation.reply({{"type", "error"}, {"error_type", "auth_error"}, {"description", "Retry sign-in"}});
    conversation.request("cancel_session");
    require(auth.inputBlocked(1), "input enabled before cancel acknowledgement");
    conversation.reply({{"type", "success"}});
    require(!auth.inputBlocked(2) && !state.authenticating && state.status == "Retry sign-in", "retry state lost");
  }

  void testTimeout() {
    Conversation conversation;
    conversation.auth.submit(1, "sample");
    conversation.request("create_session");
    conversation.auth.onGreetdTimeout({GreetdRequestType::CreateSession, std::chrono::seconds(60)});
    require(
        conversation.model.state().unavailable && !conversation.client.isConnected(), "timeout did not fail closed"
    );
    conversation.auth.submit(2, "retry");
    conversation.disconnected();
  }

  void testRemovedOwnerDuringCancel() {
    Conversation conversation;
    conversation.auth.submit(1, "sample");
    conversation.request("create_session");
    conversation.auth.onViewRemoved(2);
    require(conversation.client.isConnected(), "removing an idle view interrupted authentication");
    require(conversation.auth.showUserPicker(2), "could not request cancellation");
    require(conversation.model.state().cancelling, "cancellation state lost");
    conversation.noRequest();
    conversation.auth.onViewRemoved(1);
    require(conversation.model.state().unavailable, "removing the cancelling owner did not fail closed");
    conversation.disconnected();
  }

  void testDisconnect() {
    Conversation conversation;
    conversation.auth.submit(1, "sample");
    conversation.request("create_session");
    conversation.closePeer();
    conversation.auth.onGreetdReadable();
    require(
        conversation.model.state().unavailable && conversation.auth.inputBlocked(2), "disconnect left input enabled"
    );
    require(!conversation.client.isConnected(), "disconnect left transport active");
  }

  void testEmptyPasswordPolicy() {
    Conversation conversation(false);
    conversation.auth.submit(1, "");
    conversation.noRequest();
    require(conversation.model.state().status == "Enter a password", "empty-password policy lost");
    require(!conversation.model.state().unavailable, "empty input disabled the service");
  }

  void testHandoff() {
    Conversation conversation;
    bool exitRequested = false;
    conversation.auth.setOnExitRequested([&]() { exitRequested = true; });
    conversation.auth.submit(1, "sample");
    conversation.request("create_session");
    conversation.reply({{"type", "success"}});
    conversation.request("start_session");
    require(conversation.model.state().startingSession && !exitRequested, "handoff state lost before acknowledgement");
    require(!conversation.auth.showUserPicker(2), "account changed during handoff");
    conversation.reply({{"type", "success"}});
    require(exitRequested && !conversation.auth.cancelAuthentication(), "successful handoff was not terminal");
    conversation.noRequest();
  }
} // namespace

int main() {
  try {
    testPrompts();
    testTimeout();
    testRemovedOwnerDuringCancel();
    testDisconnect();
    testEmptyPasswordPolicy();
    testHandoff();
    std::puts("auth controller: prompts, empty input, ownership, timeout, disconnect, and handoff passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "auth controller: %s\n", error.what());
    return 1;
  }
}
