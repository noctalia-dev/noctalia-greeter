#pragma once

#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace accounts {

  struct CachedUser {
    std::string username;
    uid_t uid = 0;
    std::string iconPath;
    std::optional<std::string> realName;
  };

  // Users known to org.freedesktop.Accounts (ListCachedUsers), including
  // previously logged-in FreeIPA/LDAP accounts that NSS enumeration may miss.
  [[nodiscard]] std::vector<CachedUser> listCachedUsers();

  // Returns a readable IconFile path for uid from org.freedesktop.Accounts.
  [[nodiscard]] std::optional<std::string> iconFileForUid(uid_t uid);

} // namespace accounts
