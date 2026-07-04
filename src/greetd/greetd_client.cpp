#include "greetd/greetd_client.h"

#include "core/log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <json.hpp>
#include <poll.h>
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

  std::optional<GreetdResponse> parseResponse(const json& data) {
    if (auto err = parseError(data)) {
      return GreetdResponse{GreetdResponseType::Error, {}, *err};
    }
    if (auto msg = parseAuthMessage(data)) {
      return GreetdResponse{GreetdResponseType::AuthMessage, *msg, {}};
    }
    if (parseSuccess(data)) {
      return GreetdResponse{GreetdResponseType::Success, {}, {}};
    }
    return std::nullopt;
  }
} // namespace

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

  // Non-blocking so the event loop never stalls on a slow PAM conversation.
  const int flags = ::fcntl(m_socketFd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK) < 0) {
    kLog.error("failed to set greetd socket non-blocking: {}", strerror(errno));
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
  m_readBuffer.clear();
}

bool GreetdClient::isConnected() const noexcept { return m_socketFd >= 0; }

bool GreetdClient::writeAll(const void* data, std::size_t size) {
  const auto* p = static_cast<const char*>(data);
  std::size_t off = 0;
  while (off < size) {
    const ssize_t n = ::send(m_socketFd, p + off, size - off, MSG_NOSIGNAL);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Send buffer full; wait for it to drain (requests are tiny).
      pollfd pfd{m_socketFd, POLLOUT, 0};
      ::poll(&pfd, 1, 1000);
      continue;
    }
    return false;
  }
  return true;
}

bool GreetdClient::sendRequest(const std::string& request) {
  if (m_socketFd < 0) {
    m_lastError = {GreetdErrorType::Error, "not connected to greetd"};
    return false;
  }
  const std::uint32_t len = static_cast<std::uint32_t>(request.size());
  if (!writeAll(&len, sizeof(len)) || !writeAll(request.data(), request.size())) {
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return false;
  }
  m_lastError.reset();
  return true;
}

void GreetdClient::drainSocket() {
  char chunk[4096];
  for (;;) {
    const ssize_t n = ::recv(m_socketFd, chunk, sizeof(chunk), 0);
    if (n > 0) {
      m_readBuffer.append(chunk, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      m_lastError = {GreetdErrorType::Error, "greetd closed the connection"};
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    m_lastError = {GreetdErrorType::Error, std::string("greetd read failed: ") + strerror(errno)};
    return;
  }
}

std::optional<GreetdResponse> GreetdClient::extractFrame() {
  if (m_readBuffer.size() < sizeof(std::uint32_t)) {
    return std::nullopt;
  }
  std::uint32_t len = 0;
  std::memcpy(&len, m_readBuffer.data(), sizeof(len));
  if (m_readBuffer.size() < sizeof(len) + len) {
    return std::nullopt;
  }

  const std::string payload = m_readBuffer.substr(sizeof(len), len);
  m_readBuffer.erase(0, sizeof(len) + len);

  try {
    const json data = json::parse(payload);
    if (auto resp = parseResponse(data)) {
      return resp;
    }
    m_lastError = {GreetdErrorType::Error, "unexpected greetd response"};
    kLog.error("unexpected response: {}", data.dump());
    return std::nullopt;
  } catch (const std::exception& e) {
    m_lastError = {GreetdErrorType::Error, "failed to parse response"};
    kLog.error("failed to parse response: {}", e.what());
    return std::nullopt;
  }
}

std::optional<GreetdResponse> GreetdClient::readMessage() {
  m_lastError.reset();
  if (m_socketFd < 0) {
    m_lastError = {GreetdErrorType::Error, "not connected to greetd"};
    return std::nullopt;
  }

  // A complete frame may already be buffered from a previous drain.
  if (auto frame = extractFrame()) {
    return frame;
  }
  drainSocket();
  if (m_lastError) {
    return std::nullopt;
  }
  return extractFrame();
}

bool GreetdClient::requestCreateSession(const std::string& username) {
  json req;
  req["type"] = "create_session";
  req["username"] = username;
  return sendRequest(req.dump());
}

bool GreetdClient::requestPostAuthData(const std::string& data) {
  json req;
  req["type"] = "post_auth_message_response";
  if (!data.empty()) {
    req["response"] = data;
  }
  return sendRequest(req.dump());
}

bool GreetdClient::requestStartSession(const GreetdSessionCommand& command) {
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

  return sendRequest(req.dump());
}

bool GreetdClient::requestCancelSession() {
  json req;
  req["type"] = "cancel_session";
  return sendRequest(req.dump());
}
