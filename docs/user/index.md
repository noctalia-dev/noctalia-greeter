---
title: Noctalia Greeter
description: Install, configure, and use the Noctalia login greeter for greetd.
sidebar:
  order: 0
---

[Noctalia Greeter](https://github.com/noctalia-dev/noctalia-greeter) is a graphical login screen for **[greetd](https://github.com/kennylevinsen/greetd)**. It lets you select a user and Wayland session, authenticate, and choose a color scheme using the same visual language as Noctalia.

greetd launches `noctalia-greeter-session`, which starts the bundled wlroots compositor and runs the greeter UI. It is a focused login environment, not a desktop shell or a general-purpose compositor.

## Start here

1. [Install a distribution package or configure NixOS](installation.md).
2. If no package is available, [build the greeter from source](building-from-source.md).
3. If your package or module did not configure greetd, [point it at `noctalia-greeter-session`](installation.md#3-configure-greetd).
4. [Choose any administrator-controlled defaults](configuration.md), such as the initial user, session, or cursor.
5. Optionally [sync your Noctalia appearance](sync.md) to the login screen.

## Guides

| Guide | Use it for |
|-------|------------|
| [Installation](installation.md) | Distribution packages, NixOS modules, greetd setup, and display-manager cutover |
| [Building from source](building-from-source.md) | Build dependencies and manual installation for distributions without a package |
| [Configuration](configuration.md) | Config-file precedence, remembered state, available keys, default user, and default session |
| [Sync with Noctalia](sync.md) | Wallpapers, palette, monitor sync, Polkit, and passwordless authorization |
| [Displays](displays.md) | Output selection, multi-monitor layout, modes, transforms, scale, and idle blanking |
| [Keyboard and cursor](input.md) | Keyboard navigation, XKB layout, Num Lock, and cursor themes |
| [Troubleshooting](troubleshooting.md) | Logs and fixes for startup, login, display, sync, and input problems |

## Requirements

The greeter requires **greetd**, **D-Bus**, and the packaged assets. Your Wayland desktop sessions are installed separately.

The following integrations are optional:

- **Polkit** and `pkexec` for appearance sync from Noctalia
- **AccountsService** for user avatars
- **Noctalia Shell** for copying wallpaper, palette, font, and output settings

For packager-specific dependencies and filesystem layout, see the repository's [packaging guide](https://github.com/noctalia-dev/noctalia-greeter/blob/main/PACKAGING.md).
