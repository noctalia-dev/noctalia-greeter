---
title: Keyboard and cursor
description: Configure keyboard navigation, XKB layouts, Num Lock, and cursor themes.
sidebar:
  order: 5
---

Noctalia Greeter supports keyboard-only navigation and lets administrators configure the keyboard layout, Num Lock state, and cursor theme used on the login screen.

- [Keyboard controls](#keyboard-controls)
- [Keyboard layout](#keyboard-layout)
- [Num Lock](#num-lock)
- [Cursor theme](#cursor-theme)
- [NixOS](#nixos)

---

## Keyboard controls

The greeter works without a mouse.

| Key | Action |
|-----|--------|
| `Tab` / `Shift+Tab` | Move focus |
| `↑` / `↓` | Move focus, or move through an open menu |
| `Enter` | Submit the password, activate a control, or confirm a menu item |
| `Space` | Activate the focused control |
| `Esc` | Close a menu or return from the password step to the user list |
| `Ctrl+U` | Delete the selection or everything left of the cursor in a text field |
| `Ctrl+Backspace` / `Ctrl+Delete` | Delete word-wise in a text field. In the password field, delete everything left or right of the cursor so the masked text does not reveal spaces |
| `Ctrl+←` / `Ctrl+→` | Move the cursor word-wise in a text field. In the password field, jump to the start or end |
| `F3` | Open the session picker |
| `F7` | Open the color scheme picker |
| `XF86MonBrightnessUp` / `XF86MonBrightnessDown` | Raise or lower the screen brightness |
| `Ctrl+Alt+F1`–`F12` | Switch to a virtual terminal (TTY) |

The brightness keys adjust the backlight of the seat through logind's session
API, so they work without extra privileges while the greeter is the foreground
session. Machines without a backlight device ignore them.

Long session and color scheme menus stay within the screen and show a scrollbar
when their entries do not fit. Scroll with the mouse wheel over the menu, drag
the scrollbar thumb, or click its track. Using `↑` / `↓` automatically scrolls
the menu to keep the highlighted item visible.

---

## Keyboard layout

The compositor loads its XKB keymap in this order:

1. `[keyboard].layout`, `[keyboard].variant`, and `[keyboard].options` in `greeter.toml`
2. `XKB_DEFAULT_MODEL`, `XKB_DEFAULT_LAYOUT`, `XKB_DEFAULT_VARIANT`, and `XKB_DEFAULT_OPTIONS` from the session environment
3. XKB settings discovered from common system configuration files
4. The xkbcommon default keymap

System discovery does not depend on a particular distribution or init system.
The compositor recognizes `XKBMODEL`, `XKBLAYOUT`, `XKBVARIANT`, and
`XKBOPTIONS` assignments (including their underscore-separated forms) in
`/etc/default/keyboard`, `/etc/vconsole.conf`, and `/etc/sysconfig/keyboard`.
It also recognizes standard `XkbModel`, `XkbLayout`, `XkbVariant`, and
`XkbOptions` entries in the usual system Xorg configuration and
`xorg.conf.d` locations under `/etc`, `/usr/local/share`, and `/usr/share`.
These files are parsed as data and are never executed.

A console-only `KEYMAP` value is not used because console keymap names cannot
be mapped reliably to XKB layouts across distributions. Systems that only store
a console keymap should provide one of the XKB forms above or use
`greeter.toml`.

Terminal greeters such as `tuigreet` receive input through the virtual
terminal's kernel keymap. Noctalia Greeter is a Wayland compositor and receives
raw input events instead, so it needs an XKB configuration from one of the
sources above.

Set the layout in `/var/lib/noctalia-greeter/greeter.toml`:

```toml
[keyboard]
layout = "cz"
```

Multiple layouts can be comma-separated. Add an XKB option to choose how to switch between them:

```toml
[keyboard]
layout = "us,cz"
variant = ",qwertz"
options = "grp:alt_shift_toggle"
```

Use standard [XKB layout codes](https://man.archlinux.org/man/xkeyboard-config.7#LAYOUTS) such as `de`, `fr`, or `ru`. You can also inspect `/usr/share/X11/xkb/rules/base.lst`; systems with systemd commonly provide `localectl list-x11-keymap-layouts`.

### Environment variables

greetd starts the greeter with a minimal environment. Put XKB overrides in the greetd session `command`, rather than setting a bare service environment variable:

```toml
[default_session]
command = "env XKB_DEFAULT_LAYOUT=cz /usr/bin/noctalia-greeter-session"
```

Prefer `greeter.toml` for persistent configuration. Environment variables are mainly useful for testing or installations that cannot manage that file.

---

## Num Lock

The compositor enables Num Lock at startup by default so numeric keypads work immediately. Disable it if that causes incorrect key input:

```toml
[keyboard]
numlock = false
```

This setting has no effect when the active keymap does not provide Num Lock.

---

## Cursor theme

The compositor resolves the cursor theme, size, and search path in this order:

1. `[cursor].theme`, `[cursor].size`, and `[cursor].path` in `greeter.toml`
2. `XCURSOR_THEME`, `XCURSOR_SIZE`, and `XCURSOR_PATH` from the session environment
3. The wlroots defaults, including a built-in cursor at size `24`

Configure a system-installed theme in `greeter.toml`:

```toml
[cursor]
theme = "Adwaita"
size = 24
```

`theme` must be the exact, case-sensitive name of the directory that directly
contains the theme's `cursors/` directory. It is not necessarily the name shown
by desktop appearance settings. For example, Arch Linux installs the Breeze
cursor theme as `breeze_cursors`, not `Breeze`:

```toml
[cursor]
theme = "breeze_cursors"
size = 24
```

Install the matching system package first (`breeze-cursors` for that example,
or `adwaita-cursors` for `Adwaita` on Arch Linux). To inspect installed names,
find `cursors` directories under `/usr/share/icons`; use the name of their
parent directory.

If the theme is outside the default icon search paths, set the directory that contains it:

```toml
[cursor]
theme = "Bibata-Modern-Ice"
size = 24
path = "/usr/share/icons"
```

If the path contains no cursor theme (no `<path>/*/cursors` directory, for any theme name), the greeter also tries `<path>/share/icons`, so a package or prefix root (such as a Nix store path) works too. If neither contains any cursor theme, an error is logged and the path is used as given. A colon-separated list of directories is used as is, without this check.

The path must be readable by the greetd session user. A cursor theme installed only in your personal home directory is normally unavailable to the greeter.

If the named theme cannot be loaded, wlroots uses its built-in fallback. That
fallback looks like a default cursor and can appear too small on scaled outputs;
the compositor logs an error identifying the missing theme.

### Environment variables

As with keyboard variables, put cursor overrides in the greetd session command:

```toml
[default_session]
command = "env XCURSOR_THEME=Adwaita XCURSOR_SIZE=24 /usr/bin/noctalia-greeter-session"
```

Add `XCURSOR_PATH` when the theme is not in the default search path.

---

## NixOS

NixOS has two different Noctalia Greeter modules. Configure the option path belonging to the module you enabled; do not enable both modules at once.

### nixpkgs module

The nixpkgs module provides a `cursorTheme` convenience option. It fills the cursor theme and search path, while other cursor and keyboard values go under `settings`:

```nix
services.displayManager.noctalia-greeter = {
  enable = true;

  cursorTheme = {
    package = pkgs.bibata-cursors;
    name = "Bibata-Modern-Ice";
  };

  settings = {
    cursor.size = 24;
    keyboard = {
      layout = "us,cz";
      options = "grp:alt_shift_toggle";
      numlock = true;
    };
  };
};
```

### Project flake module

Configure the greeter through `settings`:

```nix
services.displayManager.noctalia-greeter = {
  enable = true;

  settings = {
    cursor = {
      theme = "Bibata-Modern-Ice";
      size = 24;
      path = pkgs.bibata-cursors;
    };
    keyboard = {
      layout = "us,cz";
      options = "grp:alt_shift_toggle";
      numlock = true;
    };
  };
};
```

Both modules materialize these values in `/var/lib/noctalia-greeter/greeter.toml`; neither needs to inject `XKB_DEFAULT_*` or `XCURSOR_*` into the greetd command.
