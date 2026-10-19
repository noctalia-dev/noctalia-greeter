#include "greetd/greetd_client.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

  struct TestSocket {
    std::string directory;
    std::string path;
    int listener = -1;
    int peer = -1;

    ~TestSocket() {
      if (peer >= 0) {
        ::close(peer);
      }
      if (listener >= 0) {
        ::close(listener);
      }
      if (!directory.empty()) {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
          std::cerr << "remove test socket directory: " << error.message() << '\n';
        }
      }
    }

    void open() {
      char pattern[] = "/tmp/noctalia-greetd-response-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      if (created == nullptr) {
        throw std::runtime_error("mkdtemp: " + std::string(std::strerror(errno)));
      }
      directory = created;
      path = directory + "/greetd.sock";
      listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if (listener < 0) {
        throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
      }
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      if (path.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("test socket path is too long");
      }
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
          || ::listen(listener, 1) != 0) {
        throw std::runtime_error("bind/listen: " + std::string(std::strerror(errno)));
      }
    }
  };

  void readAll(int fd, void* destination, std::size_t size) {
    auto* cursor = static_cast<char*>(destination);
    while (size > 0) {
      pollfd ready{fd, POLLIN, 0};
      const int status = ::poll(&ready, 1, 1000);
      if (status < 0 && errno == EINTR) {
        continue;
      }
      if (status <= 0) {
        throw std::runtime_error("timed out or failed while waiting for an IPC request");
      }
      const ssize_t count = ::read(fd, cursor, size);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        throw std::runtime_error("incomplete IPC request");
      }
      cursor += count;
      size -= static_cast<std::size_t>(count);
    }
  }

  [[nodiscard]] bool expectResponse(const std::optional<std::string>& response, const char* label) {
    try {
      TestSocket socket;
      socket.open();
      GreetdClient client;
      if (!client.connect(socket.path)) {
        throw std::runtime_error("greetd client could not connect to the test socket");
      }
      socket.peer = ::accept(socket.listener, nullptr, nullptr);
      if (socket.peer < 0 || !client.requestPostAuthData(response)) {
        throw std::runtime_error("greetd client could not submit the response");
      }

      std::uint32_t length = 0;
      readAll(socket.peer, &length, sizeof(length));
      if (length > 4096) {
        throw std::runtime_error("unexpected IPC request length");
      }
      std::string payload(length, '\0');
      readAll(socket.peer, payload.data(), payload.size());
      const auto request = nlohmann::json::parse(payload);
      if (request.value("type", "") != "post_auth_message_response") {
        throw std::runtime_error("unexpected IPC request type");
      }
      if (response.has_value()) {
        if (!request.contains("response")
            || !request["response"].is_string()
            || request["response"].get<std::string>() != *response) {
          throw std::runtime_error("submitted credential was omitted or changed");
        }
      } else if (request.contains("response")) {
        throw std::runtime_error("informational acknowledgement included a credential");
      }
      return true;
    } catch (const std::exception& error) {
      std::cerr << label << ": " << error.what() << '\n';
      return false;
    }
  }

} // namespace

int main() {
  bool ok = true;
  ok &= expectResponse(std::string{}, "empty credential");
  ok &= expectResponse(std::string{"synthetic-test-token"}, "nonempty credential");
  ok &= expectResponse(std::nullopt, "informational acknowledgement");
  return ok ? 0 : 1;
}
