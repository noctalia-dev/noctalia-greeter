#include "greetd/greetd_error_message.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace {

  bool expect(std::string_view name, std::string_view actual, std::string_view expected) {
    if (actual == expected) {
      return true;
    }
    std::fprintf(
        stderr, "%.*s: expected '%.*s', got '%.*s'\n", static_cast<int>(name.size()), name.data(),
        static_cast<int>(expected.size()), expected.data(), static_cast<int>(actual.size()), actual.data()
    );
    return false;
  }

  GreetdError authError(std::string description) {
    return GreetdError{.type = GreetdErrorType::AuthError, .description = std::move(description)};
  }

} // namespace

int main() {
  bool passed = true;
  passed = expect("wrong password", greetdErrorMessage(authError("pam_authenticate: AUTH_ERR")), "Incorrect password")
      && passed;
  passed = expect("unknown user", greetdErrorMessage(authError("pam_authenticate: USER_UNKNOWN")), "Incorrect password")
      && passed;
  passed = expect(
               "too many attempts", greetdErrorMessage(authError("pam_authenticate: MAXTRIES")),
               "Too many failed attempts. Try again later."
           )
      && passed;
  passed =
      expect(
          "expired password", greetdErrorMessage(authError("pam_acct_mgmt: CRED_EXPIRED")), "Your password has expired"
      )
      && passed;
  passed =
      expect(
          "expired account", greetdErrorMessage(authError("pam_acct_mgmt: ACCT_EXPIRED")), "This account has expired"
      )
      && passed;
  passed = expect(
               "login not permitted", greetdErrorMessage(authError("pam_acct_mgmt: PERM_DENIED")),
               "Login is not allowed for this account"
           )
      && passed;
  passed = expect("other auth failure", greetdErrorMessage(authError("pam_authenticate: SERVICE_ERR")), "Login failed")
      && passed;
  passed = expect(
               "non-auth error",
               greetdErrorMessage(
                   GreetdError{.type = GreetdErrorType::Error, .description = "a session is already being configured"}
               ),
               "a session is already being configured"
           )
      && passed;
  return passed ? 0 : 1;
}
