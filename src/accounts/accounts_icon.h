#pragma once

#include <optional>
#include <string>
#include <sys/types.h>

namespace accounts {

  // Returns a readable IconFile path for uid from org.freedesktop.Accounts.
  [[nodiscard]] std::optional<std::string> iconFileForUid(uid_t uid);

} // namespace accounts
