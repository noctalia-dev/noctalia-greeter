#include "greeter/build_info.h"

#include NOCTALIA_GREETER_REVISION_HEADER

namespace greeter::build_info {
  std::string_view version() noexcept { return NOCTALIA_GREETER_VERSION; }
  std::string_view revision() noexcept { return NOCTALIA_GREETER_GIT_REVISION; }
  std::string displayVersion() {
    std::string label = "v";
    label += version();
    if (!revision().empty() && revision() != "unknown") {
      label += " (";
      label += revision();
      label += ')';
    }
    return label;
  }
} // namespace greeter::build_info
