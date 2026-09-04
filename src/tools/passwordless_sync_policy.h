#pragma once

namespace greeter::passwordless_sync {

  // Implements the conventional-distribution administrator CLI. It manages a
  // dedicated site-local Polkit rule and never edits administrator-authored rules.
  [[nodiscard]] int runCommand(int argc, char* argv[]);

} // namespace greeter::passwordless_sync
