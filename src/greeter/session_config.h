#pragma once

#include <optional>
#include <string>
#include <vector>

struct GreeterSyncedSessionAction {
  std::string action;
  std::optional<std::string> command;
  std::optional<std::string> label;
  std::optional<std::string> glyph;
};

struct GreeterSyncedSession {
  struct Power {
    std::optional<std::string> suspend;
    std::optional<std::string> reboot;
    std::optional<std::string> shutdown;
  } power;
  std::vector<GreeterSyncedSessionAction> actions;
};

[[nodiscard]] std::optional<GreeterSyncedSession> loadGreeterSyncedSession();
