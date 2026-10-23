#include "greetd/greetd_error_message.h"

#include <string_view>

namespace {

  std::string_view pamReturnCode(std::string_view description) {
    const auto separator = description.rfind(": ");
    return separator == std::string_view::npos ? description : description.substr(separator + 2);
  }

} // namespace

std::string greetdErrorMessage(const GreetdError& error) {
  if (error.type != GreetdErrorType::AuthError) {
    return error.description;
  }
  const auto code = pamReturnCode(error.description);
  if (code == "MAXTRIES") {
    return "Too many failed attempts. Try again later.";
  }
  if (code == "CRED_EXPIRED") {
    return "Your password has expired";
  }
  if (code == "ACCT_EXPIRED") {
    return "This account has expired";
  }
  if (code == "PERM_DENIED") {
    return "Login is not allowed for this account";
  }
  if (code == "AUTH_ERR" || code == "USER_UNKNOWN") {
    return "Incorrect password";
  }
  return "Login failed";
}
