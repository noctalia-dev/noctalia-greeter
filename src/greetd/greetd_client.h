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

class GreetdClient {
public:
  GreetdClient();
  ~GreetdClient();

  bool connect(const std::string& socketPath);
  void disconnect();
  [[nodiscard]] bool isConnected() const noexcept;

  // Create a new session for the given username.
  // Returns an auth prompt if PAM needs input, nullopt on success, nullopt +
  // lastError() on failure.
  std::optional<GreetdAuthMessage> createSession(const std::string& username);

  // Post authentication data (password)
  std::optional<GreetdAuthMessage> postAuthData(const std::string& data);

  // Start the session with the given command
  bool startSession(const GreetdSessionCommand& command);

  bool cancelSession();

  // Get the last error
  [[nodiscard]] const std::optional<GreetdError>& lastError() const noexcept { return m_lastError; }

private:
  struct Response;
  Response sendRequest(const std::string& request);

  int m_socketFd = -1;
  std::int64_t m_sessionId = -1;
  std::optional<GreetdError> m_lastError;
};
