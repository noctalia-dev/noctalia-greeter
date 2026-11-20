#include "greeter/greeter_model.h"

#include "accounts/accounts_icon.h"
#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_preferences.h"
#include "theme/builtin_palettes.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unordered_set>
#include <utility>

namespace {
  constexpr Logger kLog("greeter-model");

  void appendDummyUsers(std::vector<std::string>& users, std::vector<uid_t>& uids) {
    const char* dummyEnv = std::getenv("NOCTALIA_GREETER_DUMMY_USERS");
    if (dummyEnv == nullptr) {
      return;
    }

    std::size_t count = 15;
    if (dummyEnv[0] != '\0') {
      char* end = nullptr;
      const unsigned long parsed = std::strtoul(dummyEnv, &end, 10);
      if (end != dummyEnv && parsed > 0) {
        count = static_cast<std::size_t>(parsed);
      }
    }

    for (std::size_t i = 0; i < count; ++i) {
      char name[32];
      std::snprintf(name, sizeof(name), "dummy-user-%02zu", i + 1);
      users.push_back(name);
      uids.push_back(static_cast<uid_t>(60'000U + i));
    }
  }

} // namespace

void GreeterModel::initialize() {
  loadUsers();
  m_sessions = greeter::discoverSessions();
  m_syncedAppearance = loadGreeterSyncedAppearance();
  m_schemeNames.clear();
  if (m_syncedAppearance.has_value()) {
    m_schemeNames.emplace_back(greeter::appearance::kSyncedSchemeDisplayName);
    m_state.selectedScheme = 0;
  }
  for (const auto& builtinPalette : noctalia::theme::builtinPalettes()) {
    m_schemeNames.emplace_back(builtinPalette.name);
    if (!m_syncedAppearance.has_value() && builtinPalette.name == "Noctalia") {
      m_state.selectedScheme = m_schemeNames.size() - 1;
    }
  }

  const auto prefs = greeter::loadGreeterPreferences();
  m_allowEmptyPassword = prefs.allowEmptyPassword;
  if (const auto session = greeter::resolveInitialSessionName(prefs)) {
    if (const auto index = greeter::findSessionIndex(m_sessions, *session)) {
      m_state.selectedSession = *index;
    }
  }
  if (prefs.scheme.has_value()) {
    const auto it = std::find(m_schemeNames.begin(), m_schemeNames.end(), *prefs.scheme);
    if (it != m_schemeNames.end()) {
      m_state.selectedScheme = static_cast<std::size_t>(it - m_schemeNames.begin());
    }
  }
  applyInitialUserSelection();
}

void GreeterModel::setOnChanged(std::function<void(GreeterViewId, bool)> callback) {
  m_onChanged = std::move(callback);
}

void GreeterModel::notifyChanged(GreeterViewId source, bool selection) {
  if (m_onChanged) {
    m_onChanged(source, selection);
  }
}

void GreeterModel::selectSession(GreeterViewId source, std::size_t index) {
  if (index >= m_sessions.size()) {
    return;
  }
  m_state.selectedSession = index;
  savePreferences();
  notifyChanged(source, true);
}

void GreeterModel::selectScheme(GreeterViewId source, std::size_t index) {
  if (index >= m_schemeNames.size()) {
    return;
  }
  m_state.selectedScheme = index;
  savePreferences();
  notifyChanged(source, true);
}

void GreeterModel::applyInitialUserSelection() {
  if (const auto initialUser = greeter::resolveInitialUserName(greeter::loadGreeterPreferences())) {
    const auto it = std::find(m_users.begin(), m_users.end(), *initialUser);
    if (it != m_users.end()) {
      m_state.selectedUser = static_cast<std::size_t>(it - m_users.begin());
    } else {
      // Configured domain accounts need not be present in NSS enumeration.
      m_users.push_back(*initialUser);
      m_userUids.push_back(0);
      m_userIconPaths.emplace_back();
      m_state.selectedUser = m_users.size() - 1;
      kLog.info("default_user '{}' not in user list; opening password step anyway", *initialUser);
    }
    m_state.username = *initialUser;
    m_state.passwordVisible = true;
    return;
  }
  m_state.passwordVisible = m_users.size() == 1;
}

void GreeterModel::loadUsers() {
  m_users.clear();
  m_userUids.clear();
  m_userIconPaths.clear();
  static const std::unordered_set<std::string> kHiddenSystemUsers = {
      "greeter", "greetd", "sddm", "lightdm", "gdm", "nobody",
  };

  std::unordered_set<std::string> seen;

  // Prefer AccountsService cached users (same source as ReGreet) so FreeIPA /
  // LDAP accounts that have logged in here appear even when NSS enumeration
  // does not list them.
  const auto cachedUsers = accounts::listCachedUsers();
  for (const auto& cached : cachedUsers) {
    if (cached.username.empty() || kHiddenSystemUsers.contains(cached.username)) {
      continue;
    }
    if (!seen.insert(cached.username).second) {
      continue;
    }
    m_users.push_back(cached.username);
    m_userUids.push_back(cached.uid);
    m_userIconPaths.push_back(cached.iconPath);
  }
  if (!cachedUsers.empty()) {
    kLog.info("AccountsService: {} cached user(s), {} after filters", cachedUsers.size(), m_users.size());
  }

  int userEnumerationErrno = 0;
  ::setpwent();
  while (true) {
    errno = 0;
    struct passwd* pw = ::getpwent();
    if (pw == nullptr) {
      userEnumerationErrno = errno;
      break;
    }

    if (pw->pw_name == nullptr || pw->pw_shell == nullptr) {
      continue;
    }

    std::string user = pw->pw_name;
    std::string shell = pw->pw_shell;
    uid_t uid = pw->pw_uid;

    if (uid < 1000 || kHiddenSystemUsers.contains(user)) {
      continue;
    }
    if (shell.find("nologin") != std::string::npos || shell.find("false") != std::string::npos) {
      continue;
    }
    if (!seen.insert(user).second) {
      continue;
    }
    m_users.push_back(user);
    m_userUids.push_back(uid);
    m_userIconPaths.push_back(accounts::iconFileForUid(uid).value_or(""));
  }
  ::endpwent();

  if (userEnumerationErrno != 0) {
    kLog.warn("failed to enumerate NSS users: {}", std::strerror(userEnumerationErrno));
  }

  if (m_users.empty()) {
    m_users.push_back("greeter");
    m_userUids.push_back(0);
    m_userIconPaths.push_back("");
  }

  appendDummyUsers(m_users, m_userUids);
  while (m_userIconPaths.size() < m_users.size()) {
    m_userIconPaths.push_back("");
  }

  m_state.selectedUser = 0;
  m_state.username = m_users[m_state.selectedUser];
}

void GreeterModel::savePreferences() const {
  greeter::GreeterPreferences prefs;
  if (m_state.selectedSession < m_sessions.size()) {
    prefs.session = m_sessions[m_state.selectedSession].name;
  }
  if (m_state.selectedScheme < m_schemeNames.size()) {
    prefs.scheme = m_schemeNames[m_state.selectedScheme];
  }

  if (!greeter::saveGreeterPreferences(prefs)) {
    kLog.warn("failed to save sync.toml (check permissions on {})", greeter::greeterSyncPath().string());
  }
}
