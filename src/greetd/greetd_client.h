#pragma once

#include <chrono>
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
  GreetdErrorType type = GreetdErrorType::Error;
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

enum class GreetdRequestType : std::uint8_t {
  CreateSession,
  PostAuthMessageResponse,
  StartSession,
  CancelSession,
};

struct GreetdRequestTimeout {
  GreetdRequestType request = GreetdRequestType::CreateSession;
  std::chrono::milliseconds elapsed{0};
};

[[nodiscard]] const char* greetdRequestTypeName(GreetdRequestType type) noexcept;

// Event-driven greetd IPC client: requests are written without blocking, replies
// are drained by readMessage() when the caller's event loop sees the fd readable.
class GreetdClient {
public:
  GreetdClient();
  ~GreetdClient();

  bool connect(const std::string& socketPath);
  void disconnect();
  [[nodiscard]] bool isConnected() const noexcept;

  // A timeout of zero disables the watchdog. The timeout is applied to each
  // request independently and is rearmed after every successful write.
  void setRequestTimeout(std::chrono::seconds timeout) noexcept;
  [[nodiscard]] int requestPollTimeoutMs() const noexcept;
  [[nodiscard]] std::optional<GreetdRequestTimeout> timedOutRequest() const noexcept;

  // Socket fd for integration into a poll()/epoll loop, or -1 when disconnected.
  [[nodiscard]] int fd() const noexcept { return m_socketFd; }

  // Write a request without waiting for its reply. False on write failure.
  bool requestCreateSession(const std::string& username);
  // nullopt acknowledges an informational message; an empty string answers a prompt.
  bool requestPostAuthData(const std::optional<std::string>& data);
  bool requestStartSession(const GreetdSessionCommand& command);
  bool requestCancelSession();

  // Next parsed reply, or nullopt when no full frame is buffered (call until it
  // returns nullopt). Also nullopt with lastError() set on socket/parse failure.
  std::optional<GreetdResponse> readMessage();

  // Get the last error
  [[nodiscard]] const std::optional<GreetdError>& lastError() const noexcept { return m_lastError; }

private:
  using Clock = std::chrono::steady_clock;

  bool sendRequest(const std::string& request, GreetdRequestType type);
  bool writeAll(const void* data, std::size_t size);
  void drainSocket();
  std::optional<GreetdResponse> extractFrame();
  void completeRequest() noexcept;

  int m_socketFd = -1;
  std::string m_readBuffer;
  std::optional<GreetdError> m_lastError;
  std::chrono::seconds m_requestTimeout{60};
  std::optional<GreetdRequestType> m_pendingRequest;
  Clock::time_point m_requestStarted{};
  Clock::time_point m_requestDeadline{};
};
