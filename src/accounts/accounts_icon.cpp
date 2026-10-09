#include "accounts/accounts_icon.h"

#include <cstdint>
#include <filesystem>
#include <gio/gio.h>
#include <optional>
#include <string>
#include <vector>

namespace accounts {

  namespace {

    std::optional<GVariant*>
    dbusUserProperty(GDBusConnection* connection, const char* userPath, const char* propertyName) {
      GError* error = nullptr;
      GVariant* result = g_dbus_connection_call_sync(
          connection, "org.freedesktop.Accounts", userPath, "org.freedesktop.DBus.Properties", "Get",
          g_variant_new("(ss)", "org.freedesktop.Accounts.User", propertyName), G_VARIANT_TYPE("(v)"),
          G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error
      );
      if (result == nullptr) {
        if (error != nullptr) {
          g_error_free(error);
        }
        return std::nullopt;
      }

      GVariant* wrapped = nullptr;
      g_variant_get(result, "(v)", &wrapped);
      g_variant_unref(result);
      return wrapped;
    }

    std::optional<std::string>
    dbusUserStringProperty(GDBusConnection* connection, const char* userPath, const char* propertyName) {
      const auto property = dbusUserProperty(connection, userPath, propertyName);
      if (!property.has_value()) {
        return std::nullopt;
      }

      std::optional<std::string> value;
      if (g_variant_is_of_type(*property, G_VARIANT_TYPE_STRING)) {
        const char* text = g_variant_get_string(*property, nullptr);
        if (text != nullptr && text[0] != '\0') {
          value = text;
        }
      }
      g_variant_unref(*property);
      return value;
    }

    std::optional<std::uint64_t>
    dbusUserUint64Property(GDBusConnection* connection, const char* userPath, const char* propertyName) {
      const auto property = dbusUserProperty(connection, userPath, propertyName);
      if (!property.has_value()) {
        return std::nullopt;
      }

      std::optional<std::uint64_t> value;
      if (g_variant_is_of_type(*property, G_VARIANT_TYPE_UINT64)) {
        value = g_variant_get_uint64(*property);
      }
      g_variant_unref(*property);
      return value;
    }

    [[nodiscard]] bool iconPathReadable(const std::string& path) {
      std::error_code ec;
      return !path.empty() && std::filesystem::is_regular_file(path, ec) && !ec;
    }

    [[nodiscard]] bool shellAllowsLogin(const std::string& shell) {
      return shell.find("nologin") == std::string::npos && shell.find("false") == std::string::npos;
    }

  } // namespace

  std::vector<CachedUser> listCachedUsers() {
    std::vector<CachedUser> users;

    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
      if (error != nullptr) {
        g_error_free(error);
      }
      return users;
    }

    GVariant* result = g_dbus_connection_call_sync(
        connection, "org.freedesktop.Accounts", "/org/freedesktop/Accounts", "org.freedesktop.Accounts",
        "ListCachedUsers", nullptr, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error
    );
    if (result == nullptr) {
      if (error != nullptr) {
        g_error_free(error);
      }
      g_object_unref(connection);
      return users;
    }

    GVariantIter* pathIter = nullptr;
    g_variant_get(result, "(ao)", &pathIter);
    const char* userPath = nullptr;
    while (g_variant_iter_next(pathIter, "&o", &userPath)) {
      if (userPath == nullptr || userPath[0] == '\0') {
        continue;
      }

      const auto username = dbusUserStringProperty(connection, userPath, "UserName");
      if (!username.has_value()) {
        continue;
      }

      const auto shell = dbusUserStringProperty(connection, userPath, "Shell").value_or("");
      if (!shell.empty() && !shellAllowsLogin(shell)) {
        continue;
      }

      CachedUser entry;
      entry.username = *username;
      if (const auto uid = dbusUserUint64Property(connection, userPath, "Uid")) {
        entry.uid = static_cast<uid_t>(*uid);
      }
      if (const auto icon = dbusUserStringProperty(connection, userPath, "IconFile")) {
        if (iconPathReadable(*icon)) {
          entry.iconPath = *icon;
        }
      }
      entry.realName = dbusUserStringProperty(connection, userPath, "RealName");
      users.push_back(std::move(entry));
    }
    g_variant_iter_free(pathIter);
    g_variant_unref(result);
    g_object_unref(connection);
    return users;
  }

  std::optional<std::string> iconFileForUid(const uid_t uid) {
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
      if (error != nullptr) {
        g_error_free(error);
      }
      return std::nullopt;
    }

    GVariant* result = g_dbus_connection_call_sync(
        connection, "org.freedesktop.Accounts", "/org/freedesktop/Accounts", "org.freedesktop.Accounts", "FindUserById",
        g_variant_new("(x)", static_cast<gint64>(uid)), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
        &error
    );
    if (result == nullptr) {
      if (error != nullptr) {
        g_error_free(error);
      }
      g_object_unref(connection);
      return std::nullopt;
    }

    const char* userPath = nullptr;
    g_variant_get(result, "(&o)", &userPath);
    const std::string userObjectPath = userPath != nullptr ? userPath : "";
    g_variant_unref(result);
    if (userObjectPath.empty()) {
      g_object_unref(connection);
      return std::nullopt;
    }

    const std::optional<std::uint64_t> reportedUid = dbusUserUint64Property(connection, userObjectPath.c_str(), "Uid");
    if (!reportedUid.has_value() || static_cast<uid_t>(*reportedUid) != uid) {
      g_object_unref(connection);
      return std::nullopt;
    }

    const std::optional<std::string> iconFile = dbusUserStringProperty(connection, userObjectPath.c_str(), "IconFile");
    g_object_unref(connection);

    if (!iconFile.has_value() || !iconPathReadable(*iconFile)) {
      return std::nullopt;
    }
    return iconFile;
  }

} // namespace accounts
