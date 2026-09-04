#include "tools/passwordless_sync_policy_xml.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

  constexpr std::string_view kHelper = "/usr/bin/noctalia-greeter-apply-appearance";

  [[nodiscard]] std::string policyWith(std::string_view body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE policyconfig PUBLIC \"-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN\" "
           "\"http://www.freedesktop.org/standards/PolicyKit/1.0/policyconfig.dtd\">\n"
           "<policyconfig>\n"
        + std::string(body)
        + "\n</policyconfig>\n";
  }

  [[nodiscard]] bool expectValidation(std::string_view name, const std::string& policy, bool expected) {
    const bool actual =
        greeter::passwordless_sync::detail::validateConstrainedPolicyXml(policy, std::filesystem::path(kHelper));
    if (actual == expected) {
      return true;
    }
    std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
    return false;
  }

} // namespace

int main(const int argc, char* argv[]) {
  const std::string validAction = "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
                                  "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
                                  "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
                                  "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>\n"
                                  "  </action>";

  bool passed = true;
  passed &= expectValidation("valid policy", policyWith(validAction), true);
  passed &= expectValidation(
      "commented argv annotation",
      policyWith(
          "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <!-- <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate> -->\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation(
      "suffix action attribute",
      policyWith(
          "  <action notid=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation(
      "nested argv annotation",
      policyWith(
          "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <defaults><annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate></defaults>\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation("duplicate action", policyWith(validAction + "\n" + validAction), false);
  passed &= expectValidation(
      "wrong helper",
      policyWith(
          "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">/tmp/helper</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation(
      "wrong argv1",
      policyWith(
          "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--setup-system</annotate>\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation(
      "duplicate annotations",
      policyWith(
          "  <action id=\"org.noctalia.greeter.sync-appearance\">\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.path\">"
          "/usr/bin/noctalia-greeter-apply-appearance</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>\n"
          "    <annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>\n"
          "  </action>"
      ),
      false
  );
  passed &= expectValidation(
      "namespaced policy",
      "<policyconfig xmlns=\"urn:unexpected\"><action id=\"org.noctalia.greeter.sync-appearance\">"
      "<annotate key=\"org.freedesktop.policykit.exec.path\">"
      "/usr/bin/noctalia-greeter-apply-appearance</annotate>"
      "<annotate key=\"org.freedesktop.policykit.exec.argv1\">--sync</annotate>"
      "</action></policyconfig>",
      false
  );
  passed &= expectValidation("malformed XML", "<policyconfig><action>", false);

  if (argc == 3) {
    std::ifstream policyFile(argv[1], std::ios::binary);
    const std::string configuredPolicy{
        std::istreambuf_iterator<char>(policyFile),
        std::istreambuf_iterator<char>(),
    };
    if (!policyFile.good() && !policyFile.eof()) {
      std::cerr << "configured policy: failed to read " << argv[1] << '\n';
      passed = false;
    } else {
      const bool valid = greeter::passwordless_sync::detail::validateConstrainedPolicyXml(
          configuredPolicy, std::filesystem::path(argv[2])
      );
      if (!valid) {
        std::cerr << "configured policy: generated policy did not validate\n";
        passed = false;
      }
    }
  } else if (argc != 1) {
    std::cerr << "usage: passwordless-sync-policy-xml-test [POLICY HELPER]\n";
    passed = false;
  }
  return passed ? 0 : 1;
}
