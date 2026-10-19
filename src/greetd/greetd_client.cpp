#include "greetd/greetd_client.h"

#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using json = nlohmann::json;

namespace {
  constexpr Logger kLog("greetd");
  constexpr auto kWriteTimeout = std::chrono::seconds(1);

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
      GreetdResponse response;
      response.type = GreetdResponseType::Error;
      response.error = std::move(*err);
      return response;
    }
    if (auto msg = parseAuthMessage(data)) {
      GreetdResponse response;
      response.type = GreetdResponseType::AuthMessage;
      response.authMessage = std::move(*msg);
      return response;
    }
    if (parseSuccess(data)) {
      return GreetdResponse{};
    }
    return std::nullopt;
  }
} // namespace

const char* greetdRequestTypeName(const GreetdRequestType type) noexcept {
  switch (type) {
  case GreetdRequestType::CreateSession:
    return "create_session";
  case GreetdRequestType::PostAuthMessageResponse:
    return "post_auth_message_response";
  case GreetdRequestType::StartSession:
    return "start_session";
  case GreetdRequestType::CancelSession:
    return "cancel_session";
  }
  return "unknown";
}

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
  completeRequest();
}

bool GreetdClient::isConnected() const noexcept { return m_socketFd >= 0; }

void GreetdClient::setRequestTimeout(const std::chrono::seconds timeout) noexcept { m_requestTimeout = timeout; }

int GreetdClient::requestPollTimeoutMs() const noexcept {
  if (!m_pendingRequest.has_value() || m_requestTimeout.count() <= 0) {
    return -1;
  }
  const auto now = Clock::now();
  if (now >= m_requestDeadline) {
    return 0;
  }
  const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(m_requestDeadline - now).count();
  return static_cast<int>(std::min<long long>(remaining, std::numeric_limits<int>::max()));
}

std::optional<GreetdRequestTimeout> GreetdClient::timedOutRequest() const noexcept {
  if (!m_pendingRequest.has_value() || m_requestTimeout.count() <= 0) {
    return std::nullopt;
  }
  const auto now = Clock::now();
  if (now < m_requestDeadline) {
    return std::nullopt;
  }
  return GreetdRequestTimeout{
      .request = *m_pendingRequest,
      .elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_requestStarted),
  };
}

bool GreetdClient::writeAll(const void* data, std::size_t size) {
  const auto* p = static_cast<const char*>(data);
  std::size_t off = 0;
  const auto deadline = Clock::now() + kWriteTimeout;
  bool firstAttempt = true;
  while (off < size) {
    if (!firstAttempt && Clock::now() >= deadline) {
      return false;
    }
    firstAttempt = false;
    const ssize_t n = ::send(m_socketFd, p + off, size - off, MSG_NOSIGNAL);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      const auto now = Clock::now();
      if (now >= deadline) {
        return false;
      }
      const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
      pollfd pfd{m_socketFd, POLLOUT, 0};
      const int timeoutMs = static_cast<int>(std::min<long long>(remaining, std::numeric_limits<int>::max()));
      const int pollResult = ::poll(&pfd, 1, timeoutMs);
      if (pollResult > 0 && (pfd.revents & POLLOUT) != 0) {
        continue;
      }
      if (pollResult < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }
    return false;
  }
  return true;
}

bool GreetdClient::sendRequest(const std::string& request, const GreetdRequestType type) {
  if (m_socketFd < 0) {
    m_lastError = {GreetdErrorType::Error, "not connected to greetd"};
    return false;
  }
  if (m_pendingRequest.has_value()) {
    m_lastError = {GreetdErrorType::Error, "another greetd request is already in progress"};
    return false;
  }

  const std::uint32_t len = static_cast<std::uint32_t>(request.size());
  std::string frame(sizeof(len), '\0');
  std::memcpy(frame.data(), &len, sizeof(len));
  frame.append(request);
  if (!writeAll(frame.data(), frame.size())) {
    // A partial stream frame cannot be recovered. Close it so callers fail
    // closed instead of issuing another request on a corrupted connection.
    disconnect();
    m_lastError = {GreetdErrorType::Error, "failed to send request"};
    return false;
  }
  m_lastError.reset();
  m_pendingRequest = type;
  m_requestStarted = Clock::now();
  m_requestDeadline = m_requestStarted + m_requestTimeout;
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
    completeRequest();
    return frame;
  }
  drainSocket();
  auto frame = extractFrame();
  if (frame.has_value()) {
    completeRequest();
    // A complete response and EOF can arrive in the same drain. Deliver the
    // response first; a subsequent read will observe the closed connection.
    m_lastError.reset();
  }
  return frame;
}

void GreetdClient::completeRequest() noexcept { m_pendingRequest.reset(); }

bool GreetdClient::requestCreateSession(const std::string& username) {
  json req;
  req["type"] = "create_session";
  req["username"] = username;
  return sendRequest(req.dump(), GreetdRequestType::CreateSession);
}

bool GreetdClient::requestPostAuthData(const std::optional<std::string>& data) {
  json req;
  req["type"] = "post_auth_message_response";
  if (data.has_value()) {
    req["response"] = *data;
  }
  return sendRequest(req.dump(), GreetdRequestType::PostAuthMessageResponse);
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

  return sendRequest(req.dump(), GreetdRequestType::StartSession);
}

bool GreetdClient::requestCancelSession() {
  json req;
  req["type"] = "cancel_session";
  return sendRequest(req.dump(), GreetdRequestType::CancelSession);
}
