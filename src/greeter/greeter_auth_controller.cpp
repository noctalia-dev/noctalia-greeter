#include "greeter/greeter_auth_controller.h"

#include "core/log.h"

#include <utility>

namespace {
  constexpr Logger kLog("greeter-auth");
  constexpr const char* kAuthCancelStatus = "Waiting for the previous login attempt to finish...";
} // namespace

GreeterAuthController::GreeterAuthController(GreeterModel& model, GreetdClient& client)
    : m_model(model), m_client(client) {}

void GreeterAuthController::setOnExitRequested(std::function<void()> callback) {
  m_onExitRequested = std::move(callback);
}

bool GreeterAuthController::inputBlocked(GreeterViewId view) const noexcept {
  return m_model.state().unavailable
      || (m_owner.has_value() && m_owner != view)
      || m_model.state().cancelling
      || awaitingReply();
}

bool GreeterAuthController::authInProgress(GreeterViewId view) const noexcept {
  return m_owner == view && m_model.state().authenticating;
}

void GreeterAuthController::notifyChanged() { m_model.notifyChanged(m_owner.value_or(0)); }

void GreeterAuthController::clearPasswordInput() { ++m_model.m_state.credentialGeneration; }

void GreeterAuthController::updateStatus(const std::string& text, bool isError) {
  m_model.m_state.status = text;
  m_model.m_state.statusIsError = isError;
}

void GreeterAuthController::submit(GreeterViewId source, const std::string& response) {
  if (!m_client.isConnected() || inputBlocked(source)) {
    return;
  }
  if (m_model.state().username.empty()) {
    updateStatus("Enter a username", true);
    notifyChanged();
    return;
  }
  if (response.empty() && !m_model.allowEmptyPassword()) {
    updateStatus("Enter a password", true);
    notifyChanged();
    return;
  }
  if (!m_authSessionStarted) {
    m_owner = source;
    m_pendingResponse = response;
    m_hasPendingResponse = true;
    m_model.m_state.authenticating = true;
    kLog.info("greetd: create_session for '{}'", m_model.state().username);
    if (!m_client.requestCreateSession(m_model.state().username)) {
      reportGreetdTransportError(
          m_client.lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return;
    }
    m_authSessionStarted = true;
    m_pendingReplies.push_back(AuthRequest::CreateSession);
    notifyChanged();
  } else if (m_model.state().waitingForInput) {
    postAuthResponse(response);
  }
}

bool GreeterAuthController::selectUser(GreeterViewId source, std::size_t index) {
  if (index >= m_model.users().size() || !cancelAuthentication()) {
    return false;
  }
  m_model.m_state.selectedUser = index;
  m_model.m_state.username = m_model.users()[index];
  m_model.m_state.passwordVisible = true;
  clearPasswordInput();
  updateStatus(
      m_model.state().cancelling || (m_owner.has_value() && m_owner != source) ? kAuthCancelStatus : "", false
  );
  m_model.notifyChanged(source, true);
  return true;
}

bool GreeterAuthController::showUserPicker(GreeterViewId source) {
  if (m_model.users().size() <= 1 || !cancelAuthentication()) {
    return false;
  }
  m_model.m_state.passwordVisible = false;
  clearPasswordInput();
  updateStatus(
      m_model.state().cancelling || (m_owner.has_value() && m_owner != source) ? kAuthCancelStatus : "", false
  );
  m_model.notifyChanged(source, true);
  return true;
}

void GreeterAuthController::onGreetdReadable() {
  for (;;) {
    const auto response = m_client.readMessage();
    if (!response.has_value()) {
      if (const auto error = m_client.lastError()) {
        reportGreetdTransportError(*error);
      }
      return;
    }
    handleGreetdResponse(*response);
  }
}

void GreeterAuthController::onGreetdTimeout(const GreetdRequestTimeout& timeout) {
  kLog.error(
      "greetd request '{}' timed out after {} ms", greetdRequestTypeName(timeout.request), timeout.elapsed.count()
  );
  // A stuck greetd worker cannot be recovered by reconnecting or respawning the UI.
  setUnavailable("Login service stopped responding. Restart greetd.");
}

void GreeterAuthController::onViewRemoved(GreeterViewId view) {
  if (m_owner == view) {
    setUnavailable("Login interrupted after the display configuration changed. Restart greetd.");
  }
}

void GreeterAuthController::setUnavailable(std::string_view reason) {
  m_client.disconnect();
  m_model.m_state.unavailable = true;
  m_model.m_state.cancelling = false;
  m_model.m_state.authenticating = false;
  m_model.m_state.waitingForInput = false;
  m_model.m_state.startingSession = false;
  m_model.m_state.promptType.reset();
  m_model.m_state.prompt.clear();
  m_authSessionStarted = false;
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  m_pendingReplies.clear();
  m_owner.reset();
  clearPasswordInput();
  updateStatus(std::string(reason), true);
  notifyChanged();
}

void GreeterAuthController::reportGreetdTransportError(const GreetdError& error) {
  kLog.error("greetd transport failed: {}", error.description);
  setUnavailable("Login service connection failed. Restart greetd.");
}

void GreeterAuthController::handleGreetdResponse(const GreetdResponse& response) {
  if (m_pendingReplies.empty()) {
    return; // unsolicited reply — nothing to correlate it with
  }
  const AuthRequest expected = m_pendingReplies.front();
  m_pendingReplies.pop_front();

  if (m_model.m_state.cancelling && expected != AuthRequest::Cancel) {
    // This reply belongs to the previous account. It must never supply a
    // prompt or start a session for the newly displayed selection.
    (void)resetAuthSession();
    notifyChanged();
    return;
  }

  const char* reqName = expected == AuthRequest::CreateSession ? "create_session"
      : expected == AuthRequest::PostAuthData                  ? "post_auth_data"
      : expected == AuthRequest::StartSession                  ? "start_session"
                                                               : "cancel_session";
  const char* respName = response.type == GreetdResponseType::AuthMessage ? "auth_message"
      : response.type == GreetdResponseType::Error                        ? "error"
                                                                          : "success";
  kLog.debug("greetd reply to {}: {}", reqName, respName);

  // Cancel ack drained the queue; re-enable input it had locked.
  if (expected == AuthRequest::Cancel) {
    if (m_model.m_state.cancelling) {
      if (response.type != GreetdResponseType::Success) {
        reportGreetdTransportError(
            response.type == GreetdResponseType::Error
                ? response.error
                : GreetdError{GreetdErrorType::Error, "Unexpected response while cancelling authentication"}
        );
        return;
      }
      m_model.m_state.cancelling = false;
      updateStatus("", false);
    }
    m_owner.reset();
    notifyChanged();
    return;
  }

  switch (response.type) {
  case GreetdResponseType::Error:
    onAuthError(response.error);
    return;
  case GreetdResponseType::AuthMessage:
    handleAuthMessage(response.authMessage);
    return;
  case GreetdResponseType::Success:
    if (expected == AuthRequest::StartSession) {
      kLog.info("session start confirmed, exiting greeter");
      if (m_onExitRequested) {
        m_onExitRequested();
      }
    } else {
      // Auth finished; launch the session.
      beginSessionStart();
    }
    return;
  }
}

void GreeterAuthController::handleAuthMessage(const GreetdAuthMessage& message) {
  m_model.m_state.promptType = message.type;
  const char* typeName = message.type == GreetdAuthMessageType::Secret ? "secret"
      : message.type == GreetdAuthMessageType::Visible                 ? "visible"
      : message.type == GreetdAuthMessageType::Error                   ? "error"
                                                                       : "info";
  kLog.info("PAM {} message: {}", typeName, message.message);

  if (message.type == GreetdAuthMessageType::Info || message.type == GreetdAuthMessageType::Error) {
    const bool isError = message.type == GreetdAuthMessageType::Error;
    if (!message.message.empty()) {
      updateStatus(message.message, isError);
    }
    if (isError) {
      clearPasswordInput();
    }
    // Ack without a credential to resume PAM; paint now so the message shows.
    if (!m_client.requestPostAuthData(std::nullopt)) {
      reportGreetdTransportError(
          m_client.lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return;
    }
    m_pendingReplies.push_back(AuthRequest::PostAuthData);
    notifyChanged();
    return;
  }

  m_model.m_state.prompt = message.message;

  // Secret / Visible prompt. Answer with already-submitted input if we have it
  // (empty is allowed), otherwise surface the prompt and wait for the user.
  if (m_hasPendingResponse) {
    const std::string response = m_pendingResponse;
    m_hasPendingResponse = false;
    m_pendingResponse.clear();
    postAuthResponse(response);
    return;
  }

  m_model.m_state.waitingForInput = true;
  if (!message.message.empty()) {
    updateStatus(message.message, false);
  }
  notifyChanged();
}

void GreeterAuthController::postAuthResponse(const std::string& data) {
  kLog.debug("greetd: post_auth_data ({} bytes)", data.size());
  if (!m_client.requestPostAuthData(data)) {
    reportGreetdTransportError(
        m_client.lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
    );
    return;
  }
  clearPasswordInput();
  m_model.m_state.waitingForInput = false;
  m_pendingReplies.push_back(AuthRequest::PostAuthData);
  notifyChanged();
}

void GreeterAuthController::beginSessionStart() {
  kLog.info("authentication successful");

  m_model.savePreferences();

  if (!m_client.isConnected()) {
    kLog.error("no greetd client to start session");
    return;
  }

  GreetdSessionCommand cmd;
  greeter::SessionOption session{
      .name = "Shell", .desktopId = {}, .command = "/bin/sh", .desktopNames = {}, .sessionType = "tty"
  };
  if (!m_model.sessions().empty() && m_model.state().selectedSession < m_model.sessions().size()) {
    session = m_model.sessions()[m_model.state().selectedSession];
  }

  // greetd cmd is argv; Exec= may be multiple tokens (e.g. "dbus-run-session gnome-session").
  // x11 sessions are wrapped in noctalia-greeter-xsession to bootstrap Xorg first.
  {
    const std::vector<std::string> argv = greeter::sessionArgv(session);
    if (argv.empty()) {
      kLog.error("session '{}' has empty Exec", session.name);
      m_model.m_state.authenticating = false;
      clearPasswordInput();
      updateStatus("Failed to start session: empty command", true);
      if (!resetAuthSession()) {
        return;
      }
      notifyChanged();
      return;
    }
    cmd.command = argv.front();
    cmd.arguments.assign(argv.begin() + 1, argv.end());
  }
  cmd.environment = greeter::sessionStartEnvironment(session);
  {
    std::string envSummary;
    for (const auto& entry : cmd.environment) {
      if (!envSummary.empty()) {
        envSummary += ' ';
      }
      envSummary += entry.key + '=' + entry.value;
    }
    kLog.info(
        "greetd: start_session session='{}' cmd='{}' args={} env=[{}]", session.name, cmd.command, cmd.arguments.size(),
        envSummary
    );
  }

  if (!m_client.requestStartSession(cmd)) {
    reportGreetdTransportError(
        m_client.lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
    );
    return;
  }

  m_pendingReplies.push_back(AuthRequest::StartSession);
  m_model.m_state.startingSession = true;
  m_model.m_state.promptType.reset();
  m_model.m_state.prompt.clear();
  notifyChanged();
}

bool GreeterAuthController::cancelAuthentication() {
  if (m_model.state().startingSession) {
    return false;
  }
  if (m_model.m_state.cancelling || (!m_authSessionStarted && !awaitingReply())) {
    return true;
  }
  m_model.m_state.cancelling = true;
  m_model.m_state.authenticating = false;
  m_model.m_state.waitingForInput = false;
  m_model.m_state.promptType.reset();
  m_model.m_state.prompt.clear();
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  clearPasswordInput();
  updateStatus(kAuthCancelStatus, false);
  // IPC is lockstep. Drain the outstanding reply before sending cancel, and
  // keep the shared owner through both that reply and the cancel ack.
  const bool cancelled = awaitingReply() || resetAuthSession();
  notifyChanged();
  return cancelled;
}

bool GreeterAuthController::resetAuthSession() {
  if (m_client.isConnected() && m_authSessionStarted) {
    if (!m_client.requestCancelSession()) {
      m_authSessionStarted = false;
      reportGreetdTransportError(
          m_client.lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return false;
    }
    // Track the ack so a later create_session reply is not mistaken for it.
    m_pendingReplies.push_back(AuthRequest::Cancel);
    m_authSessionStarted = false;
    return true;
  }
  m_authSessionStarted = false;
  m_owner.reset();
  return true;
}

void GreeterAuthController::onAuthError(const GreetdError& error) {
  m_model.m_state.authenticating = false;
  m_model.m_state.waitingForInput = false;
  m_model.m_state.startingSession = false;
  m_model.m_state.promptType.reset();
  m_model.m_state.prompt.clear();
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  m_pendingReplies.clear();
  clearPasswordInput();
  updateStatus(error.description, true);
  kLog.warn("authentication failed: {}", error.description);
  if (!resetAuthSession()) {
    return;
  }
  notifyChanged();
}
