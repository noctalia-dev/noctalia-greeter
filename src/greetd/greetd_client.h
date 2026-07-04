#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GreetdSession {
  std::int64_t id = -1;
};

enum class GreetdAuthMessageType {
  Visible,
  Secret,
  Info,
  Error,
};

struct GreetdAuthMessage {
  GreetdAuthMessageType type = GreetdAuthMessageType::Visible;
  std::string message;
};

struct GreetdEnvironmentEntry {
  std::string key;
  std::string value;
};

struct GreetdSessionCommand {
  std::string command;
  std::vector<std::string> arguments;
  std::vector<GreetdEnvironmentEntry> environment;
};

enum class GreetdErrorType {
  AuthError,
  Error,
};

struct GreetdError {
  GreetdErrorType type;
  std::string description;
};

enum class GreetdResponseType {
  AuthMessage, // greetd wants input or has a message to show
  Success,
  Error,
};

// authMessage or error is set depending on `type`.
struct GreetdResponse {
  GreetdResponseType type = GreetdResponseType::Success;
  GreetdAuthMessage authMessage;
  GreetdError error;
};

// Event-driven greetd IPC client: requests are written without blocking, replies
// are drained by readMessage() when the caller's event loop sees the fd readable.
class GreetdClient {
public:
  GreetdClient();
  ~GreetdClient();

  bool connect(const std::string& socketPath);
  void disconnect();
  [[nodiscard]] bool isConnected() const noexcept;

  // Socket fd for integration into a poll()/epoll loop, or -1 when disconnected.
  [[nodiscard]] int fd() const noexcept { return m_socketFd; }

  // Write a request without waiting for its reply. False on write failure.
  bool requestCreateSession(const std::string& username);
  bool requestPostAuthData(const std::string& data);
  bool requestStartSession(const GreetdSessionCommand& command);
  bool requestCancelSession();

  // Next parsed reply, or nullopt when no full frame is buffered (call until it
  // returns nullopt). Also nullopt with lastError() set on socket/parse failure.
  std::optional<GreetdResponse> readMessage();

  // Get the last error
  [[nodiscard]] const std::optional<GreetdError>& lastError() const noexcept { return m_lastError; }

private:
  bool sendRequest(const std::string& request);
  bool writeAll(const void* data, std::size_t size);
  void drainSocket();
  std::optional<GreetdResponse> extractFrame();

  int m_socketFd = -1;
  std::string m_readBuffer;
  std::optional<GreetdError> m_lastError;
};
