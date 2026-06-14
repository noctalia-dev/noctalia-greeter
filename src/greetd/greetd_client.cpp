#include "greetd/greetd_client.h"

#include "core/log.h"

#include <cstring>
#include <json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using json = nlohmann::json;

namespace {
  constexpr Logger kLog("greetd");

  std::optional<GreetdError> parseError(const json& data) {
    if (data.value("type", "") != "error") {
      return std::nullopt;
    }
    const auto errorType = data.value("error_type", "error");
    return GreetdError{
        errorType == "auth_error" ? GreetdErrorType::AuthError : GreetdErrorType::Error,
        data.value("description", "unknown error"),
    };
  }

  GreetdAuthMessageType parseAuthMessageType(const std::string& type) {
    if (type == "secret") {
      return GreetdAuthMessageType::Secret;
    }
    if (type == "info") {
      return GreetdAuthMessageType::Info;
    }
    if (type == "error") {
      return GreetdAuthMessageType::Error;
    }
    return GreetdAuthMessageType::Visible;
  }

  std::optional<GreetdAuthMessage> parseAuthMessage(const json& data) {
    if (data.value("type", "") != "auth_message") {
      return std::nullopt;
    }
    GreetdAuthMessage msg;
    msg.message = data.value("auth_message", "");
    msg.type = parseAuthMessageType(data.value("auth_message_type", "visible"));
    return msg;
  }

  bool parseSuccess(const json& data) { return data.value("type", "") == "success"; }
} // namespace

struct GreetdClient::Response {
  bool success = false;
  json data;
};

GreetdClient::GreetdClient() = default;

GreetdClient::~GreetdClient() { disconnect(); }

bool GreetdClient::connect(const std::string& socketPath) {
  m_socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (m_socketFd < 0) {
    kLog.error("failed to create socket: {}", strerror(errno));
    return false;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(m_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    kLog.error("failed to connect to greetd: {}", strerror(errno));
    ::close(m_socketFd);
    m_socketFd = -1;
    return false;
  }

  kLog.info("connected to greetd at {}", socketPath);
  return true;
}

void GreetdClient::disconnect() {
  if (m_socketFd >= 0) {
    ::close(m_socketFd);
    m_socketFd = -1;
  }
  m_sessionId = -1;
}

bool GreetdClient::isConnected() const noexcept { return m_socketFd >= 0; }

GreetdClient::Response GreetdClient::sendRequest(const std::string& request) {
  if (m_socketFd < 0) {
    return {false, {}};
  }

  // Send request with length prefix
  std::uint32_t len = static_cast<std::uint32_t>(request.size());
  if (::send(m_socketFd, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len)) {
    kLog.error("failed to send request length");
    return {false, {}};
  }
  if (::send(m_socketFd, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size())) {
    kLog.error("failed to send request");
    return {false, {}};
  }

  // Read response length
  std::uint32_t responseLen = 0;
  ssize_t n = ::recv(m_socketFd, &responseLen, sizeof(responseLen), 0);
  if (n != sizeof(responseLen)) {
    kLog.error("failed to read response length");
    return {false, {}};
  }

  // Read response data
  std::string response(responseLen, '\0');
  ssize_t totalRead = 0;
  while (totalRead < static_cast<ssize_t>(responseLen)) {
    n = ::recv(m_socketFd, response.data() + totalRead, responseLen - totalRead, 0);
    if (n <= 0) {
      kLog.error("failed to read response data");
      return {false, {}};
    }
    totalRead += n;
  }

  try {
    json data = json::parse(response);
    return {true, data};
  } catch (const std::exception& e) {
    kLog.error("failed to parse response: {}", e.what());
    return {false, {}};
  }
}

std::optional<GreetdAuthMessage> GreetdClient::createSession(const std::string& username) {
  json req;
  req["type"] = "create_session";
  req["username"] = username;

  auto resp = sendRequest(req.dump());
  if (!resp.success) {
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return std::nullopt;
  }

  if (auto err = parseError(resp.data)) {
    m_lastError = *err;
    return std::nullopt;
  }

  m_lastError.reset();

  if (auto msg = parseAuthMessage(resp.data)) {
    return msg;
  }

  if (parseSuccess(resp.data)) {
    return std::nullopt;
  }

  m_lastError = {GreetdErrorType::Error, "unexpected greetd response to create_session"};
  kLog.error("unexpected response: {}", resp.data.dump());
  return std::nullopt;
}

std::optional<GreetdAuthMessage> GreetdClient::postAuthData(const std::string& data) {
  json req;
  req["type"] = "post_auth_message_response";
  if (!data.empty()) {
    req["response"] = data;
  }

  auto resp = sendRequest(req.dump());
  if (!resp.success) {
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return std::nullopt;
  }

  if (auto err = parseError(resp.data)) {
    m_lastError = *err;
    return std::nullopt;
  }

  m_lastError.reset();

  if (parseSuccess(resp.data)) {
    return std::nullopt;
  }

  if (auto msg = parseAuthMessage(resp.data)) {
    return msg;
  }

  m_lastError = {GreetdErrorType::Error, "unexpected greetd response to post_auth_message_response"};
  kLog.error("unexpected response: {}", resp.data.dump());
  return std::nullopt;
}

bool GreetdClient::startSession(const GreetdSessionCommand& command) {
  json req;
  req["type"] = "start_session";

  // cmd must be a flat array: ["command", "arg1", "arg2", ...]
  json cmdArray = json::array();
  cmdArray.push_back(command.command);
  for (const auto& arg : command.arguments) {
    cmdArray.push_back(arg);
  }
  req["cmd"] = cmdArray;

  // env must be an array of "KEY=VALUE" strings
  if (!command.environment.empty()) {
    json envArray = json::array();
    for (const auto& entry : command.environment) {
      envArray.push_back(entry.key + "=" + entry.value);
    }
    req["env"] = envArray;
  }

  auto resp = sendRequest(req.dump());
  if (!resp.success) {
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return false;
  }

  if (auto err = parseError(resp.data)) {
    m_lastError = *err;
    return false;
  }

  if (!parseSuccess(resp.data)) {
    m_lastError = {GreetdErrorType::Error, "unexpected greetd response to start_session"};
    kLog.error("unexpected response: {}", resp.data.dump());
    return false;
  }

  m_lastError.reset();
  return true;
}

bool GreetdClient::cancelSession() {
  json req;
  req["type"] = "cancel_session";

  auto resp = sendRequest(req.dump());
  if (!resp.success) {
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return false;
  }

  if (auto err = parseError(resp.data)) {
    m_lastError = *err;
    return false;
  }

  if (!parseSuccess(resp.data)) {
    m_lastError = {GreetdErrorType::Error, "unexpected greetd response to cancel_session"};
    kLog.error("unexpected response: {}", resp.data.dump());
    return false;
  }

  m_lastError.reset();
  return true;
}
