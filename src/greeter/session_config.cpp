#include "greeter/session_config.h"

#include "core/log.h"
#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

  constexpr Logger kLog("greeter-session");

  std::optional<std::string> optionalString(const nlohmann::json& object, std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->is_string()) {
      return std::nullopt;
    }
    const std::string value = it->get<std::string>();
    if (value.empty()) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<GreeterSyncedSessionAction> parseAction(const nlohmann::json& item) {
    if (!item.is_object()) {
      return std::nullopt;
    }
    const auto action = optionalString(item, "action");
    if (!action.has_value()) {
      return std::nullopt;
    }
    GreeterSyncedSessionAction row;
    row.action = *action;
    row.command = optionalString(item, "command");
    row.label = optionalString(item, "label");
    row.glyph = optionalString(item, "glyph");
    return row;
  }

} // namespace

std::optional<GreeterSyncedSession> loadGreeterSyncedSession() {
  const auto path = greeterAppearanceConfigPath();
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return std::nullopt;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    kLog.warn("failed to open synced session '{}'", path.string());
    return std::nullopt;
  }

  try {
    const auto root = nlohmann::json::parse(in);
    if (!root.is_object()) {
      return std::nullopt;
    }

    const int version = root.value("version", 0);
    if (version != greeter::appearance::kManifestVersion) {
      return std::nullopt;
    }

    const auto sessionIt = root.find("session");
    if (sessionIt == root.end() || !sessionIt->is_object()) {
      return std::nullopt;
    }

    GreeterSyncedSession session;
    if (const auto powerIt = sessionIt->find("power"); powerIt != sessionIt->end() && powerIt->is_object()) {
      session.power.suspend = optionalString(*powerIt, "suspend");
      session.power.reboot = optionalString(*powerIt, "reboot");
      session.power.shutdown = optionalString(*powerIt, "shutdown");
    }

    if (const auto actionsIt = sessionIt->find("actions"); actionsIt != sessionIt->end() && actionsIt->is_array()) {
      for (const auto& item : *actionsIt) {
        if (const auto row = parseAction(item)) {
          session.actions.push_back(*row);
        }
      }
    }

    if (session.actions.empty()
        && !session.power.suspend.has_value()
        && !session.power.reboot.has_value()
        && !session.power.shutdown.has_value()) {
      return std::nullopt;
    }

    return session;
  } catch (const std::exception& e) {
    kLog.warn("failed to parse synced session from '{}': {}", path.string(), e.what());
    return std::nullopt;
  }
}
