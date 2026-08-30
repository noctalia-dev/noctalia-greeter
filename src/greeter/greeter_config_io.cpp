#include "greeter/greeter_config_io.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>

namespace {

  constexpr Logger kLog("greeter-config");
  std::vector<greeter::config::ConfigDiagnostic> g_diagnostics;

  void recordParseError(const std::filesystem::path& path, const toml::parse_error& error) {
    const auto source = error.source();
    const auto existing =
        std::ranges::find_if(g_diagnostics, [&path](const auto& diagnostic) { return diagnostic.path == path; });
    if (existing != g_diagnostics.end()) {
      return;
    }
    g_diagnostics.push_back({
        .path = path,
        .line = source.begin.line,
        .column = source.begin.column,
        .message = std::string(error.description()),
    });
    kLog.error(
        "invalid TOML configuration in {}:{}:{}: {}", path.string(), source.begin.line, source.begin.column,
        error.description()
    );
  }

  [[nodiscard]] bool isKnownTopLevelKey(std::string_view key) {
    return key == "session"
        || key == "user"
        || key == "appearance"
        || key == "output"
        || key == "cursor"
        || key == "keyboard"
        || key == "auth"
        || key == "idle";
  }

  [[nodiscard]] bool isKnownSessionKey(std::string_view key) {
    // "power"/"actions" are Sync-only (sync.toml); recognized here only so parseConfig
    // does not warn about them when parsing sync.toml through the shared session-table loop.
    return key == "default" || key == "last" || key == "power" || key == "actions";
  }

  [[nodiscard]] bool isKnownUserKey(std::string_view key) { return key == "default"; }

  [[nodiscard]] bool isKnownAppearanceKey(std::string_view key) {
    return key == "scheme"
        || key == "password_style"
        || key == "hide_logo"
        || key == "power_buttons_position"
        || key == "scheme_selector_position"
        || key == "theme_mode"
        || key == "corner_radius_scale"
        || key == "font_family"
        || key == "palette"
        || key == "wallpaper"
        || key == "wallpapers";
  }

  [[nodiscard]] bool isKnownOutputKey(std::string_view key) {
    return key == "name"
        || key == "layout"
        || key == "scale"
        || key == "scales"
        || key == "width"
        || key == "height"
        || key == "transforms";
  }

  [[nodiscard]] bool isKnownIdleKey(std::string_view key) { return key == "timeout"; }

  [[nodiscard]] bool isKnownCursorKey(std::string_view key) { return key == "theme" || key == "size" || key == "path"; }

  [[nodiscard]] bool isKnownKeyboardKey(std::string_view key) {
    return key == "layout" || key == "variant" || key == "options" || key == "numlock";
  }

  [[nodiscard]] bool isKnownAuthKey(std::string_view key) {
    return key == "allow_empty_password" || key == "autologin";
  }

  [[nodiscard]] std::optional<std::string> stringValue(const toml::node& node) {
    if (const auto value = node.value<std::string>()) {
      if (value->empty()) {
        return std::nullopt;
      }
      return *value;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<float> positiveFloatValue(const toml::node& node) {
    if (const auto value = node.value<double>()) {
      const float parsed = static_cast<float>(*value);
      if (std::isfinite(parsed) && parsed > 0.0f) {
        return parsed;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<int> cursorSizeValue(const toml::node& node) {
    if (const auto value = node.value<int64_t>()) {
      if (*value > 0 && *value <= 1024) {
        return static_cast<int>(*value);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<int> modeDimensionValue(const toml::node& node) {
    if (const auto value = node.value<int64_t>()) {
      if (*value > 0 && *value <= 16384) {
        return static_cast<int>(*value);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<int> idleTimeoutValue(const toml::node& node) {
    if (const auto value = node.value<int64_t>()) {
      if (*value >= 0 && *value <= 86400) {
        return static_cast<int>(*value);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] greeter::config::GreeterTomlWallpaper parseWallpaperTable(const toml::table& table) {
    greeter::config::GreeterTomlWallpaper wallpaper;
    if (const auto* pathNode = table.get("path")) {
      wallpaper.path = stringValue(*pathNode);
    }
    if (const auto* fillModeNode = table.get("fill_mode")) {
      wallpaper.fillMode = stringValue(*fillModeNode);
    }
    if (const auto* fillColorNode = table.get("fill_color")) {
      wallpaper.fillColor = stringValue(*fillColorNode);
    }
    return wallpaper;
  }

  void warnUnknownTopLevelKey(const std::filesystem::path& path, std::string_view key) {
    kLog.warn("{}: unrecognized top-level key '{}' (ignored)", path.string(), key);
  }

  void warnUnknownSectionKey(const std::filesystem::path& path, std::string_view section, std::string_view key) {
    kLog.warn("{}: unrecognized key '{}.{}' (ignored)", path.string(), section, key);
  }

  [[nodiscard]] greeter::config::GreeterConfigFile
  parseConfig(const toml::table& root, const std::filesystem::path& path) {
    greeter::config::GreeterConfigFile config;

    for (const auto& [key, node] : root) {
      const auto keyView = key.str();
      if (!node.is_table()) {
        if (!isKnownTopLevelKey(keyView)) {
          warnUnknownTopLevelKey(path, keyView);
        } else {
          kLog.warn("{}: expected '[{}]' table", path.string(), keyView);
        }
        continue;
      }
      if (!isKnownTopLevelKey(keyView)) {
        warnUnknownTopLevelKey(path, keyView);
        continue;
      }

      const toml::table& section = *node.as_table();
      for (const auto& [entryKey, entryNode] : section) {
        const auto entryView = entryKey.str();
        if (keyView == "session") {
          if (!isKnownSessionKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "default") {
            config.sessionDefault = stringValue(entryNode);
          } else if (entryView == "last") {
            config.sessionLast = stringValue(entryNode);
          }
          // power/actions: Sync-only, parsed directly from the root table by parseSync.
        } else if (keyView == "user") {
          if (!isKnownUserKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          config.userDefault = stringValue(entryNode);
        } else if (keyView == "appearance") {
          if (!isKnownAppearanceKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "scheme") {
            config.appearanceScheme = stringValue(entryNode);
          } else if (entryView == "password_style") {
            config.appearancePasswordStyle = stringValue(entryNode);
          } else if (entryView == "hide_logo") {
            if (const auto value = entryNode.value<bool>()) {
              config.appearanceHideLogo = *value;
            }
          } else if (entryView == "power_buttons_position") {
            config.appearancePowerButtonsPosition = stringValue(entryNode);
          } else if (entryView == "scheme_selector_position") {
            config.appearanceSchemeSelectorPosition = stringValue(entryNode);
          } else if (entryView == "theme_mode") {
            config.appearance.themeMode = stringValue(entryNode);
          } else if (entryView == "corner_radius_scale") {
            if (const auto value = entryNode.value<double>()) {
              const float parsed = static_cast<float>(*value);
              if (std::isfinite(parsed) && parsed >= 0.0f) {
                config.appearance.cornerRadiusScale = parsed;
              } else {
                kLog.warn("{}: invalid appearance.corner_radius_scale value", path.string());
              }
            } else {
              kLog.warn("{}: invalid appearance.corner_radius_scale value", path.string());
            }
          } else if (entryView == "font_family") {
            config.appearance.fontFamily = stringValue(entryNode);
          } else if (entryView == "palette") {
            if (const auto* paletteTable = entryNode.as_table()) {
              for (const auto& [paletteKey, paletteNode] : *paletteTable) {
                if (const auto value = stringValue(paletteNode)) {
                  config.appearance.palette[std::string(paletteKey.str())] = *value;
                }
              }
            } else {
              kLog.warn("{}: appearance.palette must be a table", path.string());
            }
          } else if (entryView == "wallpaper") {
            if (const auto* wallpaperTable = entryNode.as_table()) {
              config.appearance.wallpaper = parseWallpaperTable(*wallpaperTable);
            } else {
              kLog.warn("{}: appearance.wallpaper must be a table", path.string());
            }
          } else if (entryView == "wallpapers") {
            if (const auto* wallpapersTable = entryNode.as_table()) {
              for (const auto& [connectorKey, connectorNode] : *wallpapersTable) {
                if (const auto* connectorTable = connectorNode.as_table()) {
                  config.appearance.wallpapers[std::string(connectorKey.str())] = parseWallpaperTable(*connectorTable);
                } else {
                  kLog.warn("{}: appearance.wallpapers.{} must be a table", path.string(), connectorKey.str());
                }
              }
            } else {
              kLog.warn("{}: appearance.wallpapers must be a table", path.string());
            }
          }
        } else if (keyView == "output") {
          if (!isKnownOutputKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "name") {
            config.outputName = stringValue(entryNode);
          } else if (entryView == "layout") {
            config.outputLayout = stringValue(entryNode);
          } else if (entryView == "scale") {
            if (const auto scale = positiveFloatValue(entryNode)) {
              config.outputScale = *scale;
            } else {
              kLog.warn("{}: invalid output.scale value", path.string());
            }
          } else if (entryView == "width") {
            if (const auto width = modeDimensionValue(entryNode)) {
              config.outputModeWidth = *width;
            } else {
              kLog.warn("{}: invalid output.width value", path.string());
            }
          } else if (entryView == "height") {
            if (const auto height = modeDimensionValue(entryNode)) {
              config.outputModeHeight = *height;
            } else {
              kLog.warn("{}: invalid output.height value", path.string());
            }
          } else if (entryView == "transforms") {
            config.outputTransforms = stringValue(entryNode);
          } else if (entryView == "scales") {
            config.outputScales = stringValue(entryNode);
          }
        } else if (keyView == "idle") {
          if (!isKnownIdleKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "timeout") {
            if (const auto timeout = idleTimeoutValue(entryNode)) {
              config.idleTimeoutSec = *timeout;
            } else {
              kLog.warn("{}: invalid idle.timeout value", path.string());
            }
          }
        } else if (keyView == "cursor") {
          if (!isKnownCursorKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "theme") {
            config.cursorTheme = stringValue(entryNode);
          } else if (entryView == "path") {
            config.cursorPath = stringValue(entryNode);
          } else if (const auto size = cursorSizeValue(entryNode)) {
            config.cursorSize = *size;
          } else {
            kLog.warn("{}: invalid cursor.size value", path.string());
          }
        } else if (keyView == "keyboard") {
          if (!isKnownKeyboardKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (entryView == "layout") {
            config.keyboardLayout = stringValue(entryNode);
          } else if (entryView == "variant") {
            config.keyboardVariant = stringValue(entryNode);
          } else if (entryView == "options") {
            config.keyboardOptions = stringValue(entryNode);
          } else if (entryView == "numlock") {
            if (const auto value = entryNode.value<bool>()) {
              config.keyboardNumlock = *value;
            }
          }
        } else if (keyView == "auth") {
          if (!isKnownAuthKey(entryView)) {
            warnUnknownSectionKey(path, keyView, entryView);
            continue;
          }
          if (const auto value = entryNode.value<bool>()) {
            if (entryView == "allow_empty_password") {
              config.authAllowEmptyPassword = *value;
            } else {
              config.authAutoLogin = *value;
            }
          }
        }
      }
    }

    return config;
  }

  template <typename InsertFn>
  void
  insertString(toml::table& table, std::string_view key, const std::optional<std::string>& value, InsertFn insert) {
    if (value.has_value() && !value->empty()) {
      insert(table, key, *value);
    }
  }

  [[nodiscard]] toml::table buildWallpaperTomlTable(const greeter::config::GreeterTomlWallpaper& wallpaper) {
    toml::table table;
    insertString(table, "path", wallpaper.path, [](toml::table& t, std::string_view key, const std::string& value) {
      t.insert_or_assign(std::string(key), value);
    });
    insertString(
        table, "fill_mode", wallpaper.fillMode, [](toml::table& t, std::string_view key, const std::string& value) {
          t.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        table, "fill_color", wallpaper.fillColor, [](toml::table& t, std::string_view key, const std::string& value) {
          t.insert_or_assign(std::string(key), value);
        }
    );
    return table;
  }

  // Shared by greeter.toml and sync.toml: theme_mode/corner_radius_scale/font_family/palette/wallpaper(s).
  // Callers add their own scheme/password_style/hide_logo keys on top.
  [[nodiscard]] toml::table buildAppearanceTomlTable(const greeter::config::GreeterTomlAppearance& appearance) {
    toml::table table;
    insertString(
        table, "theme_mode", appearance.themeMode, [](toml::table& t, std::string_view key, const std::string& value) {
          t.insert_or_assign(std::string(key), value);
        }
    );
    if (appearance.cornerRadiusScale.has_value()) {
      table.insert_or_assign("corner_radius_scale", static_cast<double>(*appearance.cornerRadiusScale));
    }
    insertString(
        table, "font_family", appearance.fontFamily,
        [](toml::table& t, std::string_view key, const std::string& value) {
          t.insert_or_assign(std::string(key), value);
        }
    );
    if (!appearance.palette.empty()) {
      toml::table palette;
      for (const auto& [key, value] : appearance.palette) {
        if (!value.empty()) {
          palette.insert_or_assign(key, value);
        }
      }
      if (!palette.empty()) {
        table.insert("palette", std::move(palette));
      }
    }
    if (appearance.wallpaper.has_value()) {
      toml::table wallpaper = buildWallpaperTomlTable(*appearance.wallpaper);
      if (!wallpaper.empty()) {
        table.insert("wallpaper", std::move(wallpaper));
      }
    }
    if (!appearance.wallpapers.empty()) {
      toml::table wallpapers;
      for (const auto& [connector, wallpaper] : appearance.wallpapers) {
        toml::table wallpaperTable = buildWallpaperTomlTable(wallpaper);
        if (!wallpaperTable.empty()) {
          wallpapers.insert(connector, std::move(wallpaperTable));
        }
      }
      if (!wallpapers.empty()) {
        table.insert("wallpapers", std::move(wallpapers));
      }
    }
    return table;
  }

  [[nodiscard]] toml::table buildTomlTable(const greeter::config::GreeterConfigFile& config) {
    toml::table root;

    toml::table session;
    insertString(
        session, "default", config.sessionDefault,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!session.empty()) {
      root.insert("session", std::move(session));
    }

    toml::table user;
    insertString(
        user, "default", config.userDefault, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!user.empty()) {
      root.insert("user", std::move(user));
    }

    toml::table appearance = buildAppearanceTomlTable(config.appearance);
    insertString(
        appearance, "scheme", config.appearanceScheme,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        appearance, "password_style", config.appearancePasswordStyle,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (config.appearanceHideLogo.has_value()) {
      appearance.insert_or_assign("hide_logo", *config.appearanceHideLogo);
    }
    insertString(
        appearance, "power_buttons_position", config.appearancePowerButtonsPosition,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        appearance, "scheme_selector_position", config.appearanceSchemeSelectorPosition,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!appearance.empty()) {
      root.insert("appearance", std::move(appearance));
    }

    toml::table output;
    insertString(
        output, "name", config.outputName, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        output, "layout", config.outputLayout, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (config.outputScale.has_value()) {
      output.insert_or_assign("scale", static_cast<double>(*config.outputScale));
    }
    if (config.outputModeWidth.has_value()) {
      output.insert_or_assign("width", static_cast<int64_t>(*config.outputModeWidth));
    }
    if (config.outputModeHeight.has_value()) {
      output.insert_or_assign("height", static_cast<int64_t>(*config.outputModeHeight));
    }
    insertString(
        output, "transforms", config.outputTransforms,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        output, "scales", config.outputScales, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!output.empty()) {
      root.insert("output", std::move(output));
    }

    if (config.idleTimeoutSec.has_value()) {
      toml::table idle;
      idle.insert_or_assign("timeout", static_cast<int64_t>(*config.idleTimeoutSec));
      root.insert("idle", std::move(idle));
    }

    toml::table cursor;
    insertString(
        cursor, "theme", config.cursorTheme, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (config.cursorSize.has_value()) {
      cursor.insert_or_assign("size", static_cast<int64_t>(*config.cursorSize));
    }
    insertString(
        cursor, "path", config.cursorPath, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!cursor.empty()) {
      root.insert("cursor", std::move(cursor));
    }

    toml::table keyboard;
    insertString(
        keyboard, "layout", config.keyboardLayout,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        keyboard, "variant", config.keyboardVariant,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        keyboard, "options", config.keyboardOptions,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (config.keyboardNumlock.has_value()) {
      keyboard.insert_or_assign("numlock", *config.keyboardNumlock);
    }
    if (!keyboard.empty()) {
      root.insert("keyboard", std::move(keyboard));
    }

    if (config.authAllowEmptyPassword.has_value()) {
      toml::table auth;
      auth.insert_or_assign("allow_empty_password", *config.authAllowEmptyPassword);
      if (config.authAutoLogin.has_value()) {
        auth.insert_or_assign("autologin", *config.authAutoLogin);
      }
      root.insert("auth", std::move(auth));
    } else if (config.authAutoLogin.has_value()) {
      toml::table auth;
      auth.insert_or_assign("autologin", *config.authAutoLogin);
      root.insert("auth", std::move(auth));
    }

    return root;
  }

  [[nodiscard]] toml::table buildSyncTomlTable(const greeter::config::GreeterSyncFile& sync) {
    toml::table root;

    toml::table session;
    insertString(
        session, "last", sync.sessionLast, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    toml::table power;
    insertString(
        power, "suspend", sync.sessionPowerSuspend,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        power, "reboot", sync.sessionPowerReboot,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        power, "shutdown", sync.sessionPowerShutdown,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!power.empty()) {
      session.insert("power", std::move(power));
    }
    if (!sync.sessionActions.empty()) {
      toml::array actions;
      for (const auto& action : sync.sessionActions) {
        if (action.action.empty()) {
          continue;
        }
        toml::table row;
        row.insert_or_assign("action", action.action);
        insertString(
            row, "command", action.command, [](toml::table& table, std::string_view key, const std::string& value) {
              table.insert_or_assign(std::string(key), value);
            }
        );
        insertString(
            row, "label", action.label, [](toml::table& table, std::string_view key, const std::string& value) {
              table.insert_or_assign(std::string(key), value);
            }
        );
        insertString(
            row, "glyph", action.glyph, [](toml::table& table, std::string_view key, const std::string& value) {
              table.insert_or_assign(std::string(key), value);
            }
        );
        actions.push_back(std::move(row));
      }
      if (!actions.empty()) {
        session.insert("actions", std::move(actions));
      }
    }
    if (!session.empty()) {
      root.insert("session", std::move(session));
    }

    toml::table appearance = buildAppearanceTomlTable(sync.appearance);
    insertString(
        appearance, "scheme", sync.appearanceScheme,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!appearance.empty()) {
      root.insert("appearance", std::move(appearance));
    }

    toml::table output;
    insertString(
        output, "layout", sync.outputLayout, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        output, "transforms", sync.outputTransforms,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        output, "scales", sync.outputScales, [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    if (!output.empty()) {
      root.insert("output", std::move(output));
    }

    return root;
  }

  [[nodiscard]] std::vector<greeter::config::GreeterSyncFile::SyncSessionAction>
  parseSyncSessionActions(const toml::array& array, const std::filesystem::path& path) {
    std::vector<greeter::config::GreeterSyncFile::SyncSessionAction> actions;
    for (const auto& node : array) {
      const auto* table = node.as_table();
      if (table == nullptr) {
        kLog.warn("{}: session.actions entries must be tables", path.string());
        continue;
      }
      const auto* actionNode = table->get("action");
      const auto action = actionNode != nullptr ? stringValue(*actionNode) : std::nullopt;
      if (!action.has_value()) {
        kLog.warn("{}: session.actions entry is missing 'action'", path.string());
        continue;
      }
      greeter::config::GreeterSyncFile::SyncSessionAction row;
      row.action = *action;
      if (const auto* commandNode = table->get("command")) {
        row.command = stringValue(*commandNode);
      }
      if (const auto* labelNode = table->get("label")) {
        row.label = stringValue(*labelNode);
      }
      if (const auto* glyphNode = table->get("glyph")) {
        row.glyph = stringValue(*glyphNode);
      }
      actions.push_back(std::move(row));
    }
    return actions;
  }

  [[nodiscard]] greeter::config::GreeterSyncFile parseSync(const toml::table& root, const std::filesystem::path& path) {
    // Reuse config parser, then keep only sync keys (including the Sync-owned appearance table).
    const greeter::config::GreeterConfigFile full = parseConfig(root, path);
    greeter::config::GreeterSyncFile sync;
    sync.sessionLast = full.sessionLast;
    sync.appearanceScheme = full.appearanceScheme;
    sync.outputLayout = full.outputLayout;
    sync.outputTransforms = full.outputTransforms;
    sync.outputScales = full.outputScales;
    sync.appearance = full.appearance;

    if (const auto* sessionNode = root.get("session")) {
      if (const auto* sessionTable = sessionNode->as_table()) {
        if (const auto* powerNode = sessionTable->get("power")) {
          if (const auto* powerTable = powerNode->as_table()) {
            if (const auto* n = powerTable->get("suspend")) {
              sync.sessionPowerSuspend = stringValue(*n);
            }
            if (const auto* n = powerTable->get("reboot")) {
              sync.sessionPowerReboot = stringValue(*n);
            }
            if (const auto* n = powerTable->get("shutdown")) {
              sync.sessionPowerShutdown = stringValue(*n);
            }
          } else {
            kLog.warn("{}: session.power must be a table", path.string());
          }
        }
        if (const auto* actionsNode = sessionTable->get("actions")) {
          if (const auto* actionsArray = actionsNode->as_array()) {
            sync.sessionActions = parseSyncSessionActions(*actionsArray, path);
          } else {
            kLog.warn("{}: session.actions must be an array of tables", path.string());
          }
        }
      }
    }

    return sync;
  }

  [[nodiscard]] std::string formatToml(const toml::table& table) {
    std::ostringstream out;
    out << toml::toml_formatter{
        table, toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings
    };
    return out.str();
  }

  void copyString(char* out, const std::size_t outSize, const std::optional<std::string>& value) {
    if (outSize == 0) {
      return;
    }
    out[0] = '\0';
    if (!value.has_value() || value->empty()) {
      return;
    }
    std::snprintf(out, outSize, "%s", value->c_str());
  }

} // namespace

namespace greeter::config {

  bool GreeterTomlAppearance::hasCompletePalette() const {
    for (const auto key : greeter::appearance::requiredPaletteKeys()) {
      const auto it = palette.find(std::string(key));
      if (it == palette.end() || it->second.empty()) {
        return false;
      }
    }
    return true;
  }

  GreeterConfigFile loadConfig(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
      return {};
    }

    try {
      const toml::table table = toml::parse_file(path.string());
      return parseConfig(table, path);
    } catch (const toml::parse_error& e) {
      recordParseError(path, e);
      return {};
    }
  }

  bool writeConfig(const std::filesystem::path& path, const GreeterConfigFile& config) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // Declarative file only — the last-used session lives in sync.toml.
    GreeterConfigFile declarative = config;
    declarative.sessionLast.reset();

    const toml::table table = buildTomlTable(declarative);

    std::ostringstream out;
    out << "# noctalia-greeter greeter.toml (declarative; Nix-safe; UI and Sync never write this)\n";
    out << "# Last-used session lives in sync.toml; UI/Sync also fall back to sync.toml for scheme\n";
    out << "# and output layout/transforms when not set here. Session power actions/menu entries are\n";
    out << "# Sync-only (sync.toml [session.power]/[[session.actions]]) and are not settable here.\n";
    out << "# [session] default, [user] default\n";
    out << "# [appearance] scheme, password_style, hide_logo, power_buttons_position, scheme_selector_position, "
           "theme_mode, corner_radius_scale, font_family\n";
    out << "# [appearance.palette] full color role table, [appearance.wallpaper] path/fill_mode/fill_color\n";
    out << "# [appearance.wallpapers.<connector>] per-output wallpaper overrides\n";
    out << "# [output] name/layout/scale/scales/width/height/transforms, [idle] timeout, [cursor] theme/size/path\n";
    out << "# [keyboard] layout/variant/options/numlock\n";
    out << "# [auth] allow_empty_password, autologin (bool; autologin requires [user].default)\n";
    out << '\n';
    out << formatToml(table);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      kLog.warn("failed to open '{}' for write", path.string());
      return false;
    }
    const std::string content = out.str();
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
  }

  GreeterSyncFile loadSync(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
      return {};
    }

    try {
      const toml::table table = toml::parse_file(path.string());
      return parseSync(table, path);
    } catch (const toml::parse_error& e) {
      recordParseError(path, e);
      return {};
    }
  }

  bool writeSync(const std::filesystem::path& path, const GreeterSyncFile& sync) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const toml::table table = buildSyncTomlTable(sync);

    std::ostringstream out;
    out << "# noctalia-greeter sync.toml (UI + Sync; not managed by Nix)\n";
    out << "# [session] last, [session.power] suspend/reboot/shutdown, [[session.actions]] "
           "action/command/label/glyph\n";
    out << "# [appearance] scheme, theme_mode, corner_radius_scale, font_family\n";
    out << "# [appearance.palette] full color role table, [appearance.wallpaper]/[appearance.wallpapers.<connector>]\n";
    out << "# [output] layout/transforms/scales\n";
    out << '\n';
    out << formatToml(table);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      kLog.warn("failed to open '{}' for write", path.string());
      return false;
    }
    const std::string content = out.str();
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
  }

  void clearConfigDiagnostics() { g_diagnostics.clear(); }

  const std::vector<ConfigDiagnostic>& configDiagnostics() { return g_diagnostics; }

} // namespace greeter::config

namespace {

  [[nodiscard]] std::optional<std::string>
  preferString(const std::optional<std::string>& preferred, const std::optional<std::string>& fallback) {
    if (preferred.has_value() && !preferred->empty()) {
      return preferred;
    }
    if (fallback.has_value() && !fallback->empty()) {
      return fallback;
    }
    return std::nullopt;
  }

  constexpr const char* kLegacyStateTomlFileName = "state.toml";

  void migrateLegacyRuntimeKeysToSync(const std::filesystem::path& confPath, const std::filesystem::path& syncPath) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(syncPath, ec) && !ec) {
      return;
    }

    // Prior releases named this file state.toml; adopt it once under the new name.
    const auto legacyStatePath = syncPath.parent_path() / kLegacyStateTomlFileName;
    const bool hasLegacyState = std::filesystem::is_regular_file(legacyStatePath, ec) && !ec;
    greeter::config::GreeterSyncFile sync =
        hasLegacyState ? greeter::config::loadSync(legacyStatePath) : greeter::config::GreeterSyncFile{};

    greeter::config::GreeterConfigFile conf = greeter::config::loadConfig(confPath);
    const bool hasRuntime = (conf.sessionLast.has_value() && !conf.sessionLast->empty())
        || (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty())
        || (conf.outputLayout.has_value() && !conf.outputLayout->empty())
        || (conf.outputTransforms.has_value() && !conf.outputTransforms->empty())
        || (conf.outputScales.has_value() && !conf.outputScales->empty());
    if (!hasLegacyState && !hasRuntime) {
      return;
    }

    if (conf.sessionLast.has_value() && !conf.sessionLast->empty()) {
      sync.sessionLast = conf.sessionLast;
    }
    if (conf.appearanceScheme.has_value() && !conf.appearanceScheme->empty()) {
      sync.appearanceScheme = conf.appearanceScheme;
    }
    if (conf.outputLayout.has_value() && !conf.outputLayout->empty()) {
      sync.outputLayout = conf.outputLayout;
    }
    if (conf.outputTransforms.has_value() && !conf.outputTransforms->empty()) {
      sync.outputTransforms = conf.outputTransforms;
    }
    if (conf.outputScales.has_value() && !conf.outputScales->empty()) {
      sync.outputScales = conf.outputScales;
    }
    if (!greeter::config::writeSync(syncPath, sync)) {
      kLog.warn("failed to migrate runtime keys to {}", syncPath.string());
      return;
    }

    if (hasRuntime) {
      conf.sessionLast.reset();
      conf.appearanceScheme.reset();
      conf.outputLayout.reset();
      conf.outputTransforms.reset();
      conf.outputScales.reset();
      if (!greeter::config::writeConfig(confPath, conf)) {
        kLog.warn("migrated sync.toml but failed to strip runtime keys from {}", confPath.string());
        return;
      }
      kLog.info("migrated runtime keys from greeter.toml into {}", syncPath.string());
    } else {
      kLog.info("migrated {} to {}", legacyStatePath.string(), syncPath.string());
    }
  }

} // namespace

extern "C" void greeter_compositor_config_load(const char* state_dir, struct greeter_compositor_config* out) {
  if (out == nullptr) {
    return;
  }

  std::memset(out, 0, sizeof(*out));

  const char* dir = state_dir;
  if (dir == nullptr || dir[0] == '\0') {
    dir = greeter::appearance::kDefaultSyncedDataDir;
  }

  const auto confPath = std::filesystem::path(dir) / greeter::appearance::kGreeterTomlFileName;
  const auto syncPath = std::filesystem::path(dir) / greeter::appearance::kSyncTomlFileName;
  migrateLegacyRuntimeKeysToSync(confPath, syncPath);

  const greeter::config::GreeterConfigFile config = greeter::config::loadConfig(confPath);
  const greeter::config::GreeterSyncFile sync = greeter::config::loadSync(syncPath);

  copyString(out->preferred_output, sizeof(out->preferred_output), config.outputName);
  copyString(out->cursor_theme, sizeof(out->cursor_theme), config.cursorTheme);
  copyString(out->cursor_path, sizeof(out->cursor_path), config.cursorPath);
  copyString(out->keyboard_layout, sizeof(out->keyboard_layout), config.keyboardLayout);
  copyString(out->keyboard_variant, sizeof(out->keyboard_variant), config.keyboardVariant);
  copyString(out->keyboard_options, sizeof(out->keyboard_options), config.keyboardOptions);
  if (config.keyboardNumlock.has_value()) {
    out->keyboard_numlock = *config.keyboardNumlock ? 1 : -1;
  }
  // greeter.toml wins; otherwise Sync/UI sync.toml.
  copyString(out->output_layout, sizeof(out->output_layout), preferString(config.outputLayout, sync.outputLayout));
  copyString(
      out->output_transforms, sizeof(out->output_transforms),
      preferString(config.outputTransforms, sync.outputTransforms)
  );
  copyString(out->output_scales, sizeof(out->output_scales), preferString(config.outputScales, sync.outputScales));

  if (config.outputScale.has_value() && *config.outputScale >= 1.0f) {
    out->manual_scale = *config.outputScale;
  }
  if (config.outputModeWidth.has_value() && *config.outputModeWidth > 0) {
    out->manual_mode_width = *config.outputModeWidth;
  }
  if (config.outputModeHeight.has_value() && *config.outputModeHeight > 0) {
    out->manual_mode_height = *config.outputModeHeight;
  }
  if (config.idleTimeoutSec.has_value() && *config.idleTimeoutSec >= 0) {
    out->idle_timeout_sec = *config.idleTimeoutSec;
  }
  if (config.cursorSize.has_value() && *config.cursorSize > 0) {
    out->cursor_size = *config.cursorSize;
  }
}
