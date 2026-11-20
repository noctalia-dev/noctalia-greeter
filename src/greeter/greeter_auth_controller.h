#pragma once

#include "greetd/greetd_client.h"
#include "greeter/greeter_model.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Owns the one greetd conversation independently of the lifetime of any view.
class GreeterAuthController {
public:
  GreeterAuthController(GreeterModel& model, GreetdClient& client);

  void setOnExitRequested(std::function<void()> callback);
  void submit(GreeterViewId source, const std::string& response);
  [[nodiscard]] bool selectUser(GreeterViewId source, std::size_t index);
  [[nodiscard]] bool showUserPicker(GreeterViewId source);
  [[nodiscard]] bool cancelAuthentication();
  void onGreetdReadable();
  void onGreetdTimeout(const GreetdRequestTimeout& timeout);
  void setUnavailable(std::string_view reason);
  void onViewRemoved(GreeterViewId view);

  [[nodiscard]] bool inputBlocked(GreeterViewId view) const noexcept;
  [[nodiscard]] bool authInProgress(GreeterViewId view) const noexcept;

private:
  enum class AuthRequest { CreateSession, PostAuthData, StartSession, Cancel };
  [[nodiscard]] bool awaitingReply() const noexcept { return !m_pendingReplies.empty(); }
  void handleGreetdResponse(const GreetdResponse& response);
  void handleAuthMessage(const GreetdAuthMessage& message);
  void postAuthResponse(const std::string& data);
  void beginSessionStart();
  void onAuthError(const GreetdError& error);
  void reportGreetdTransportError(const GreetdError& error);
  [[nodiscard]] bool resetAuthSession();
  void clearPasswordInput();
  void updateStatus(const std::string& text, bool isError);
  void notifyChanged();

  GreeterModel& m_model;
  GreetdClient& m_client;
  std::optional<GreeterViewId> m_owner;
  bool m_authSessionStarted = false;
  bool m_hasPendingResponse = false;
  std::string m_pendingResponse;
  std::deque<AuthRequest> m_pendingReplies;
  std::function<void()> m_onExitRequested;
};
