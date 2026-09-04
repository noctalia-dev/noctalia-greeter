---
title: Keyboard and cursor
description: Configure keyboard navigation, XKB layouts, Num Lock, and cursor themes.
sidebar:
  order: 5
---

# Keyboard and cursor

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
| `F3` | Open the session picker |
| `F7` | Open the color scheme picker |
| `Ctrl+Alt+F1`–`F12` | Switch to a virtual terminal (TTY) |

---

## Keyboard layout

The compositor loads its XKB keymap in this order:

1. `[keyboard].layout`, `[keyboard].variant`, and `[keyboard].options` in `greeter.toml`
2. `XKB_DEFAULT_LAYOUT`, `XKB_DEFAULT_VARIANT`, and `XKB_DEFAULT_OPTIONS` from the session environment
3. The system default keymap

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

If the theme is outside the default icon search paths, set the directory that contains it:

```toml
[cursor]
theme = "Bibata-Modern-Ice"
size = 24
path = "/usr/share/icons"
```

The path must be readable by the greetd session user. A cursor theme installed only in your personal home directory is normally unavailable to the greeter.

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

The project flake module uses `programs.noctalia-greeter.settings`. It does not have a cursor package option, so point `cursor.path` at the package's `share/icons` directory:

```nix
programs.noctalia-greeter = {
  enable = true;

  settings = {
    cursor = {
      theme = "Bibata-Modern-Ice";
      size = 24;
      path = "${pkgs.bibata-cursors}/share/icons";
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
