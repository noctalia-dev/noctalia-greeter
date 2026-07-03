#include "greeter/greeter_config_io.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>

namespace {

  constexpr Logger kLog("greeter-config");

  [[nodiscard]] bool isKnownTopLevelKey(std::string_view key) {
    return key == "session"
        || key == "user"
        || key == "appearance"
        || key == "output"
        || key == "cursor"
        || key == "keyboard";
  }

  [[nodiscard]] bool isKnownSessionKey(std::string_view key) { return key == "default" || key == "last"; }

  [[nodiscard]] bool isKnownUserKey(std::string_view key) { return key == "default"; }

  [[nodiscard]] bool isKnownAppearanceKey(std::string_view key) {
    return key == "scheme" || key == "password_style" || key == "hide_logo";
  }

  [[nodiscard]] bool isKnownOutputKey(std::string_view key) {
    return key == "name" || key == "layout" || key == "scale";
  }

  [[nodiscard]] bool isKnownCursorKey(std::string_view key) { return key == "theme" || key == "size" || key == "path"; }

  [[nodiscard]] bool isKnownKeyboardKey(std::string_view key) {
    return key == "layout" || key == "variant" || key == "options" || key == "numlock";
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
          } else {
            config.sessionLast = stringValue(entryNode);
          }
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
          } else if (const auto scale = positiveFloatValue(entryNode)) {
            config.outputScale = *scale;
          } else {
            kLog.warn("{}: invalid output.scale value", path.string());
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

  [[nodiscard]] toml::table buildTomlTable(const greeter::config::GreeterConfigFile& config) {
    toml::table root;

    toml::table session;
    insertString(
        session, "default", config.sessionDefault,
        [](toml::table& table, std::string_view key, const std::string& value) {
          table.insert_or_assign(std::string(key), value);
        }
    );
    insertString(
        session, "last", config.sessionLast, [](toml::table& table, std::string_view key, const std::string& value) {
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

    toml::table appearance;
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
    if (!output.empty()) {
      root.insert("output", std::move(output));
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

    return root;
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

  GreeterConfigFile loadConfig(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
      return {};
    }

    try {
      const toml::table table = toml::parse_file(path.string());
      return parseConfig(table, path);
    } catch (const toml::parse_error& e) {
      kLog.warn("failed to parse {}: {}", path.string(), e.description());
      return {};
    }
  }

  bool writeConfig(const std::filesystem::path& path, const GreeterConfigFile& config) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const toml::table table = buildTomlTable(config);

    std::ostringstream out;
    out << "# noctalia-greeter greeter.toml\n";
    out << "# [session] default/last, [user] default, [appearance] scheme/password_style/hide_logo\n";
    out << "# [output] name/layout/scale, [cursor] theme/size/path, [keyboard] layout/variant/options/numlock\n";
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

} // namespace greeter::config

extern "C" void greeter_compositor_config_load(const char* state_dir, struct greeter_compositor_config* out) {
  if (out == nullptr) {
    return;
  }

  std::memset(out, 0, sizeof(*out));

  const char* dir = state_dir;
  if (dir == nullptr || dir[0] == '\0') {
    dir = greeter::appearance::kDefaultSyncedDataDir;
  }

  const auto path = std::filesystem::path(dir) / greeter::appearance::kGreeterTomlFileName;
  const greeter::config::GreeterConfigFile config = greeter::config::loadConfig(path);

  copyString(out->preferred_output, sizeof(out->preferred_output), config.outputName);
  copyString(out->cursor_theme, sizeof(out->cursor_theme), config.cursorTheme);
  copyString(out->cursor_path, sizeof(out->cursor_path), config.cursorPath);
  copyString(out->keyboard_layout, sizeof(out->keyboard_layout), config.keyboardLayout);
  copyString(out->keyboard_variant, sizeof(out->keyboard_variant), config.keyboardVariant);
  copyString(out->keyboard_options, sizeof(out->keyboard_options), config.keyboardOptions);
  if (config.keyboardNumlock.has_value()) {
    out->keyboard_numlock = *config.keyboardNumlock ? 1 : -1;
  }
  copyString(out->output_layout, sizeof(out->output_layout), config.outputLayout);

  if (config.outputScale.has_value() && *config.outputScale >= 1.0f) {
    out->manual_scale = *config.outputScale;
  }
  if (config.cursorSize.has_value() && *config.cursorSize > 0) {
    out->cursor_size = *config.cursorSize;
  }
}
