---
title: Configuration
description: Configure greeter.toml defaults and understand mutable sync.toml state.
sidebar:
  order: 2
---

# Configuration

Administrator-controlled settings live in `/var/lib/noctalia-greeter/greeter.toml`. Noctalia Sync and choices made on the login screen use the lower-priority `sync.toml`; they never rewrite `greeter.toml`.

- [Configuration files](#configuration-files)
- [Keys the greeter remembers](#keys-the-greeter-remembers)
- [Configuration reference](#configuration-reference)
- [Default session](#default-session)
- [Default user](#default-user)
- [Full example](#full-example)

## Configuration files

| Path | Role |
|------|------|
| `/var/lib/noctalia-greeter/greeter.toml` | Full declarative configuration. Wins over Sync when the same value is set |
| `/var/lib/noctalia-greeter/sync.toml` | Mutable Sync and UI state: appearance, last session/scheme, session actions, and output layout |
| `/var/lib/noctalia-greeter/wallpaper*` | Wallpaper images installed by Sync |

If `greeter.toml` is missing, the greeter uses built-in defaults. System setup creates the state directory and files owned by the greetd session user.

On NixOS, use `services.displayManager.noctalia-greeter.settings` with the nixpkgs module or `programs.noctalia-greeter.settings` with the project flake. Both materialize `greeter.toml` using a tmpfiles `L+` entry.

The **Synced** scheme uses a complete `[appearance.palette]` from `greeter.toml` when present, otherwise the same keys from `sync.toml`. Legacy live `appearance.json` is migrated into `sync.toml` once. See [Sync with Noctalia](sync.md) for the complete precedence and authorization model.

## Keys the greeter remembers

When you change the selected session or color scheme on the login screen, the greeter writes:

| Key in `sync.toml` | Purpose |
|--------------------|---------|
| `[session].last` | Last Wayland session selected, using its desktop-entry `Name=` |
| `[appearance].scheme` | Last color scheme selected, unless `greeter.toml` pins one |

Appearance Sync sets the scheme to **Synced** while preserving the last session and existing session commands.

## Configuration reference

Set these keys in `greeter.toml`. A command-line `--session` or `--user` value takes precedence over the corresponding default.

| Key | Purpose |
|-----|---------|
| `[session].default` | Session selected on startup; overrides `[session].last` |
| `[user].default` | Username selected on startup; opens the password step |
| `[appearance].scheme` | Color scheme: `Synced` or a built-in name such as `Noctalia` |
| `[appearance].password_style` | Password mask: `default` or `random` |
| `[appearance].hide_logo` | Hide the Noctalia brand logo |
| `[appearance].power_buttons_position` | Power controls: `bottom-right` (default), `bottom-left`, `top-left`, `top-right`, or `hidden` |
| `[appearance].scheme_selector_position` | Scheme picker: `top-right` (default), `top-left`, `bottom-left`, `bottom-right`, or `hidden` |
| `[appearance].theme_mode` | Theme mode for the Synced appearance, such as `dark` |
| `[appearance].corner_radius_scale` | Corner-radius scale for the Synced appearance |
| `[appearance].font_family` | Fontconfig family for the Synced appearance |
| `[appearance.palette]` | Complete Synced palette; takes precedence over Sync appearance |
| `[appearance.wallpaper]` | Default wallpaper `path`, `fill_mode`, and `fill_color` |
| `[appearance.wallpapers.<connector>]` | Per-output wallpaper override |
| `[output].name` | Connector on which to pin the greeter |
| `[output].layout` | Multi-monitor positions; overrides synced layout |
| `[output].width` / `.height` | Preferred DRM mode size |
| `[output].transforms` | Per-connector DRM transform; overrides synced transforms |
| `[output].scales` | Per-connector scale; overrides synced scales |
| `[output].scale` | Manual UI scale for every output |
| `[idle].timeout` | Seconds before outputs blank; `0` disables |
| `[cursor].theme` / `.size` / `.path` | Cursor theme |
| `[keyboard].layout` / `.variant` / `.options` / `.numlock` | XKB keymap |
| `[auth].allow_empty_password` | Permit empty submission for fprintd or smartcard PAM |
| `[auth].request_timeout` | Seconds to wait for each greetd reply (`0`–`3600`, default `60`); `0` disables the watchdog |

Display and input settings have task-oriented guides:

- [Displays](displays.md): connectors, layout, mode, transforms, scale, and idle blanking
- [Keyboard and cursor](input.md): navigation, XKB, Num Lock, and cursor themes

## Default session

The value is the desktop entry's exact **`Name=`**—the same text shown in the picker—not the `.desktop` filename. List available names with:

```sh
noctalia-greeter sessions
```

Sessions are discovered from `wayland-sessions` directories under
`/usr/local/share`, `/usr/share`, `/run/current-system/sw/share`, and each base
path in `XDG_DATA_DIRS`. Name lookup is case-insensitive, but using the exact
picker spelling keeps the configuration unambiguous.

Set the default declaratively, especially when it contains spaces or punctuation:

```toml
[session]
default = "Hyprland (uwsm-managed)"
```

Alternatively, a simple name can be passed through the greetd command:

```toml
command = "/usr/bin/noctalia-greeter-session -- --session niri"
```

Resolution order is command-line `--session`, `[session].default`, remembered `[session].last`, then the first discovered session. An unknown name is ignored.

Before PAM opens the selected session, the greeter passes `XDG_SESSION_TYPE=wayland` and derives `XDG_CURRENT_DESKTOP` and `XDG_SESSION_DESKTOP` from the desktop entry's `DesktopNames=`. This helps environments that expect systemd-managed session metadata. GNOME support remains best-effort compared with GDM.

## Default user

To open directly on one account's password step:

```toml
[user]
default = "alice"
```

You can instead pass `--user alice` after the session wrapper's `--`. Use the exact login name from `/etc/passwd`. **Esc** or the back button returns to the user list.

Resolution order is command-line `--user`, then `[user].default`, then the user picker.

## Full example

Use the maintained [full `greeter.toml` example](https://github.com/noctalia-dev/noctalia-greeter/blob/main/examples/greeter.toml) as the canonical reference. Copy it to `/var/lib/noctalia-greeter/greeter.toml`, then remove the values you do not want to override.

Restart greetd after changing startup, output, input, or idle settings. Appearance and picker state are otherwise read the next time the greeter starts.
