#include "greeter/greeter.h"
#include "wayland/wayland_client.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
  std::atomic<bool> shutdownRequested{false};
}

// Replace only main(): run the production UI, socket handling, and event loop.
// Inject synthetic input through the same entry point as the Wayland keyboard.
int main() {
  std::signal(SIGTERM, [](int) { shutdownRequested = true; });
  try {
    WaylandClient client;
    if (!client.connect()) {
      return 1;
    }
    Greeter greeter;
    if (!greeter.initialize(client)) {
      return 1;
    }
    // Dispatch the initial configure and focus the password field before typing.
    for (int i = 0; i < 3; ++i) {
      if (wl_display_roundtrip(client.display()) < 0) {
        return 1;
      }
    }
    for (const char ch : "synthetic-test-token") {
      if (ch != '\0') {
        const auto key = static_cast<std::uint32_t>(ch);
        greeter.onKeyboardEvent(key, key, 0, true, false);
        greeter.onKeyboardEvent(key, key, 0, false, false);
      }
    }
    greeter.onKeyboardEvent(XKB_KEY_Return, 0, 0, true, false);
    const int result = greeter.run(client, shutdownRequested);
    if (result == 0 && !shutdownRequested) {
      std::puts("login flow: session handoff completed");
    }
    return result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "login flow: %s\n", error.what());
    return 1;
  }
}
