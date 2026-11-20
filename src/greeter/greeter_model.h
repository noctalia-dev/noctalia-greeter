#pragma once

#include "greetd/greetd_client.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_sessions.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

using GreeterViewId = std::uint64_t;

// Shared presentation state contains no credentials or scene objects.
struct GreeterState {
  std::string username;
  std::size_t selectedUser = 0;
  std::size_t selectedSession = 0;
  std::size_t selectedScheme = 0;
  bool passwordVisible = false;
  std::string status;
  bool statusIsError = false;
  bool authenticating = false;
  bool unavailable = false;
  bool cancelling = false;
  bool waitingForInput = false;
  bool startingSession = false;
  std::optional<GreetdAuthMessageType> promptType;
  // Views clear their own input when the generation changes; no secret crosses views.
  std::uint64_t credentialGeneration = 0;
};

class GreeterAuthController;

class GreeterModel {
public:
  void initialize();
  // The flag identifies a selection change, so other views can close stale menus.
  void setOnChanged(std::function<void(GreeterViewId, bool)> callback);

  [[nodiscard]] const GreeterState& state() const noexcept { return m_state; }
  [[nodiscard]] const std::vector<std::string>& users() const noexcept { return m_users; }
  [[nodiscard]] const std::vector<std::string>& userIconPaths() const noexcept { return m_userIconPaths; }
  [[nodiscard]] const std::vector<greeter::SessionOption>& sessions() const noexcept { return m_sessions; }
  [[nodiscard]] const std::vector<std::string>& schemeNames() const noexcept { return m_schemeNames; }
  [[nodiscard]] const std::optional<GreeterSyncedAppearance>& syncedAppearance() const noexcept {
    return m_syncedAppearance;
  }
  [[nodiscard]] bool allowEmptyPassword() const noexcept { return m_allowEmptyPassword; }

  void selectSession(GreeterViewId source, std::size_t index);
  void selectScheme(GreeterViewId source, std::size_t index);
  void savePreferences() const;

private:
  friend class GreeterAuthController;

  void loadUsers();
  void applyInitialUserSelection();
  void notifyChanged(GreeterViewId source, bool selection = false);

  GreeterState m_state;
  std::vector<std::string> m_users;
  std::vector<uid_t> m_userUids;
  std::vector<std::string> m_userIconPaths;
  std::vector<greeter::SessionOption> m_sessions;
  std::vector<std::string> m_schemeNames;
  std::optional<GreeterSyncedAppearance> m_syncedAppearance;
  bool m_allowEmptyPassword = false;
  std::function<void(GreeterViewId, bool)> m_onChanged;
};
