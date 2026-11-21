---
title: Configuration
description: Configure greeter.toml defaults and understand mutable sync.toml state.
sidebar:
  order: 2
---

Administrator-controlled settings live in `/var/lib/noctalia-greeter/greeter.toml`. Noctalia Sync and choices made on the login screen use the lower-priority `sync.toml`; they never rewrite `greeter.toml`.

- [Configuration files](#configuration-files)
- [Keys the greeter remembers](#keys-the-greeter-remembers)
- [Configuration reference](#configuration-reference)
- [Default session](#default-session)
- [Default user](#default-user)
- [Component layout](#component-layout)
- [Clock](#clock)
- [Full example](#full-example)

## Configuration files

| Path | Role |
|------|------|
| `/var/lib/noctalia-greeter/greeter.toml` | Full declarative configuration. Wins over Sync when the same value is set |
| `/var/lib/noctalia-greeter/sync.toml` | Mutable Sync and UI state: appearance, last session/scheme, session actions, and output layout |
| `/var/lib/noctalia-greeter/wallpaper*` | Wallpaper images installed by Sync |

If `greeter.toml` is missing, the greeter uses built-in defaults. System setup creates the state directory and files owned by the greetd session user.

On NixOS, use `services.displayManager.noctalia-greeter.settings` with both the nixpkgs, and project flake modules. Both materialize `greeter.toml` using a tmpfiles `L+` entry.

Startup migration copies legacy runtime values into writable `sync.toml` and
preserves `greeter.toml` byte for byte, including writable files, comments, and
symlink targets. Declarative scheme, layout, transform, and scale settings keep
their priority over later Sync updates and login-screen choices.

The **Synced** scheme uses a complete `[appearance.palette]` from `greeter.toml` when present, otherwise the same keys from `sync.toml`. Legacy live `appearance.json` is migrated into `sync.toml` once. See [Sync with Noctalia](sync.md) for the complete precedence and authorization model.

Wallpaper configuration is independent of the selected color scheme and does
not require an `[appearance.palette]`. A wallpaper declared in `greeter.toml`
therefore also works with built-in schemes such as `Noctalia`. JPEG XL (`.jxl`)
wallpapers are supported for both default and per-output paths.

## Keys the greeter remembers

When you change the selected session or color scheme on the login screen, the greeter writes:

| Key in `sync.toml` | Purpose |
|--------------------|---------|
| `[session].last` | Last Wayland session selected, using its desktop-entry `Name=` |
| `[appearance].scheme` | Last color scheme selected, unless `greeter.toml` pins one |

Appearance Sync sets the scheme to **Synced** while preserving the last session and existing session commands.

If `sync.toml` contains invalid TOML, login-screen saves and legacy appearance
migration leave the file untouched. Repair the reported error before retrying.
The next read refreshes that file's diagnostics, so a repaired file can be saved
without restarting the greeter; errors in other configuration files remain
reported.

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
| `[appearance].scheme_selector_position` | Scheme picker: `bottom-right` (default), `top-left`, `bottom-left`, `bottom-right`, or `hidden` |
| `[appearance].theme_mode` | Theme mode for the Synced appearance, such as `dark` |
| `[appearance].corner_radius_scale` | Corner-radius scale for the Synced appearance |
| `[appearance].font_family` | Fontconfig family for the Synced appearance |
| `[appearance.palette]` | Complete Synced palette; takes precedence over Sync appearance |
| `[appearance.wallpaper]` | Default wallpaper `path`, `fill_mode`, `fill_color`, `blur`, and `tint`; `fill_color` also works without an image, `fill_mode` accepts `center`, `crop`, `fit`, `stretch`, `repeat`, or `span`, and `blur`/`tint` take an intensity from `0.0` (off, default) to `1.0` |
| `[appearance.wallpapers.<connector>]` | Per-output wallpaper override using the same fill modes |
| `[clock].enabled` | Show the clock; defaults to `true` |
| `[clock].position` | `center-left` (default), `above-panel`, `top-left`, `top-center`, `top-right`, `bottom-left`, `bottom-center`, or `bottom-right` |
| `[clock].time_format` | Time line format; defaults to `{:%H:%M}` |
| `[clock].date_format` | Date line format; defaults to `%A, %x` |
| `[clock].time_font_size` | Time font size before UI scaling; positive number, default `112` |
| `[clock].date_font_size` | Date font size before UI scaling; positive number, default `19` |
| `[output].name` | Connector or stable EDID identifier on which to pin the greeter |
| `[output].use_synced_settings` | Use synced layout, transforms, and scales when not declared here; defaults to `true`. Set `false` to use declarative output settings and compositor defaults only |
| `[output].layout` | Multi-monitor positions by connector or stable identifier; overrides synced layout |
| `[output].width` / `.height` | Preferred DRM mode size |
| `[output].refresh_rate` | Preferred DRM mode refresh rate in hertz, globally or per output |
| `[output].transforms` | Per-output DRM transform by connector or stable identifier; overrides synced transforms |
| `[output].scales` | Per-output scale by connector or stable identifier; overrides synced scales |
| `[output].scale` | Manual UI scale for every output |
| `[idle].timeout` | Seconds before outputs blank; `0` disables |
| `[cursor].theme` / `.size` / `.path` | Cursor theme |
| `[pointer].accel_profile` | Libinput acceleration profile: `flat` (default) or `adaptive` |
| `[pointer].accel_speed` | Libinput speed from `-1.0` to `1.0`; omitted leaves the device's speed unchanged |
| `[keyboard].layout` / `.variant` / `.options` / `.numlock` | XKB keymap |
| `[auth].allow_empty_password` | Permit an empty submission to start fprintd or smartcard PAM; this does not make password and fingerprint checks run in parallel |
| `[auth].request_timeout` | Seconds to wait for each greetd reply (`0`–`3600`, default `60`); `0` disables the watchdog |

Display and input settings have task-oriented guides:

- [Displays](displays.md): connectors, layout, mode, transforms, scale, and idle blanking
- [Keyboard and cursor](input.md): navigation, XKB, Num Lock, and cursor themes

PAM handles authentication methods in the order configured by the system. In
particular, `pam_fprintd` cannot accept a password while it is waiting for a
fingerprint. See [Fingerprint blocks password login](troubleshooting.md#fingerprint-blocks-password-login)
for the limitation and configuration options.

## Default session

The value is the desktop entry's **`Name=`**, which is the same text shown in the picker. The `.desktop` filename without the extension is also accepted as a fallback; a `Name=` match wins if both apply. List available names with:

```sh
noctalia-greeter sessions
```

Sessions are discovered from `wayland-sessions` and `xsessions` directories
under `/usr/local/share`, `/usr/share`, `/run/current-system/sw/share`, and
each base path in `XDG_DATA_DIRS`. Both name and filename lookup are
case-insensitive, but using the exact picker spelling keeps the configuration
unambiguous. If a `wayland-sessions` and an `xsessions` entry share the same
`Name=`, the `wayland-sessions` entry wins.

An `xsessions` entry's `Exec=` is run through `noctalia-greeter-xsession`,
which bootstraps `Xorg` via `startx` before running it — `startx` handles
`DISPLAY`/`Xauthority` setup for the client. This requires `xinit` (for
`startx`) to be installed; without it, the session fails with a clear
"startx not found" error instead of starting. The wrapper runs:

```sh
startx <session Exec=> -- -seat "${XDG_SEAT:-seat0}" -keeptty vt${XDG_VTNR}
```

`-seat`/`vt${XDG_VTNR}` let Xorg get the GPU device and VT through
elogind/systemd-logind instead of needing root: `-seat` gets the device
handoff, and the explicit `vtN` is required separately because Xorg only
skips its normally-root-only `/dev/tty0` probe when a VT number is given
directly on the command line. `XDG_SEAT`/`XDG_VTNR` come from the PAM
session the same way `XDG_SESSION_TYPE` does. The wrapper also resolves a
bare `Exec=` program name (e.g. `Exec=bspwm`) to an absolute path before
handing it to `startx`, since some `startx` implementations only accept the
client as a direct path and silently fall back to their default client
(`xterm`) otherwise. See
[X11 session doesn't start](troubleshooting.md#x11-session-doesnt-start) if
it still fails.

Set the default declaratively, especially when it contains spaces or punctuation:

```toml
[session]
default = "Hyprland (uwsm-managed)"
```

Alternatively, a simple name or the `.desktop` filename can be passed through the greetd command:

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

You can instead pass `--user alice` after the session wrapper's `--`. Use the exact login name from `/etc/passwd`. **Esc** returns to the user list. In the full card, the account row also opens account search.

Resolution order is command-line `--user`, then `[user].default`, then the user picker.

## Component layout

The full card retains its position and field spacing while account selection/search is open. The credential field stays disabled until an account is selected.

The default UI places the clock and host name on the left, the login card on the right, small branding at the top left, and keyboard layout, Appearance, and Power at the bottom right. It uses the current Noctalia default palette. The keyboard indicator shows the active compositor keymap and is read-only. Power opens a menu; suspend, restart, shutdown, and firmware restart require confirmation.

When both a default user and a discovered default session are configured (through CLI or `greeter.toml`), the card automatically uses the compact presentation: identity, credential field, and Sign in. A remembered last session alone does not enable it. Missing session defaults use the full card. Press **Esc** to choose another account or session; selecting a different account/session restores the full card.

Configure components only in `greeter.toml`. The existing appearance sync payload is unchanged; the UI and sync helper never write these settings.

```toml
[layout]
login_mode = "auto"

[layout.login]
anchor = "center-right"
width = 420

[layout.clock]
anchor = "center-left"
x = 16

[layout.branding]
visible = false
```

`login_mode` accepts `auto` (default), `full` (always show pickers), or `minimal` (use the compact card when valid defaults are available, otherwise show the full card). Component IDs are `login`, `clock`, `branding`, `power`, `appearance`, and `keyboard`. Settings are partial overrides of built-in defaults, resolved by ID; there are no component lists or removal operations. Hide an optional component with `visible = false`.

| Field | Values and behavior |
| --- | --- |
| `visible` | Boolean. Login is required: `false` is rejected for `login`. |
| `anchor` | `top-left`, `top-center`, `top-right`, `center-left`, `center`, `center-right`, `bottom-left`, `bottom-center`, `bottom-right`. |
| `x`, `y` | Finite logical-pixel offsets from the anchor, from -8192 to 8192. Final placement is clamped inside the output. |
| `width` | Finite logical pixels, from 32 to 1600; login width is further bounded to 280–640 and available space. |
| `output` | Optional exact connector name or stable monitor identifier. An absent/disconnected target hides that optional component. Rejected for `login`, which remains on every active greeter output. |

Explicit component fields override corresponding `[clock]` and `[appearance]` keys. When a component field is absent, clock enabled/position, hide-logo, power position, and scheme position remain fallbacks. Removing an override restores that fallback or the built-in default. `above-panel` positions the clock above the card when there is room. Unknown IDs/fields, wrong types, invalid anchors, and out-of-range/non-finite values are ignored individually and reported in the startup log and configuration banner. Valid sibling fields remain active. No layout version field is currently required; unsupported keys are ignored with diagnostics.

Placements use output-local logical coordinates. At desktop sizes the card and clock use matching outer gutters (6% of width, bounded to 64–128 logical pixels). Branding and footer use 48-pixel gutters. Widths below 1000 or heights below 600 use the narrow composition with smaller margins. Login keeps priority, followed by clock, branding, Power, Appearance, and keyboard; colliding optional components move inward or disappear. Layout reserves space for configuration errors. Very small views uniformly scale a minimum 320×520 logical canvas, including hit testing, so credential controls stay reachable. Each view calculates its own geometry; fractional/mixed output scaling does not mutate global UI metrics. Configured fonts, palettes, and wallpapers still use the existing appearance rules.

## Clock

The clock is enabled by default at the left of the screen, with the host name below the date. The login card sits near the right edge. It uses the active palette's `on_surface` color for the time and `on_surface_variant` for the date, including when the **Synced** scheme is selected.

```toml
[clock]
enabled = true
position = "center-left"
time_format = "{:%H:%M}"
date_format = "%A, %x"
time_font_size = 112
date_font_size = 19
```

Set `time_font_size` and `date_font_size` independently, in logical pixels before UI scaling. Both accept positive integers or decimals and follow the greeter's automatic or manual display scale. Omitted or invalid values use the defaults shown above.

Formats accept the same forms as Noctalia: bare `strftime` patterns such as `%H:%M`, or C++ chrono fields such as `{:%H:%M}`. Literal text is preserved, `\n` adds a line break, `%%` prints `%`, and `{{` / `}}` print braces. Common tokens include `%H` (24-hour hour), `%I` (12-hour hour), `%M` (minute), `%S` (second), `%p` (AM/PM), `%A` (weekday), `%B` (month), `%d` (day), `%Y` (year), and `%x` (locale date). The timezone follows the greeter host.

The default `%A, %x` matches Noctalia: the weekday followed by the locale's preferred date. Names and date ordering follow the greeter process's locale (`LC_ALL`, then `LC_TIME`, then `LANG`), which can differ from your desktop session's locale.

When all three environment variables are unset or empty, the greeter reads `LC_TIME` (or `LANG` if absent) from `/etc/locale.conf`, falling back to `/etc/default/locale` if the first file has no usable setting. With neither file configured, it tries `en_US.UTF-8`, then `C.UTF-8`, then `C`, using the first installed locale. This fallback sets only the time locale; explicit environment settings always take precedence. A configured locale that is not installed produces a warning in the startup log.

To choose a different date locale for the greeter, put it in greetd's session command, using a locale installed on the system:

```toml
[default_session]
command = "env LC_TIME=en_GB.UTF-8 /usr/local/bin/noctalia-greeter-session"
```

Keep your existing binary path and arguments. `LC_ALL` takes precedence over `LC_TIME`, so remove or adjust an existing `LC_ALL` override too. Restart greetd after changing the command. See [Date format or language differs from the desktop](troubleshooting.md#date-format-or-language-differs-from-the-desktop) for diagnostics.

Set either format to an empty string to hide that line, or set `enabled = false` to hide the clock and host name. Existing corner positions and `above-panel` remain accepted. Component layout settings take precedence over the matching legacy position/visibility setting. Optional components stack inward when they collide and are hidden if they cannot fit; the login card has priority. On narrow displays the time size is capped at 56 logical pixels.

## Full example

Use the maintained [full `greeter.toml` example](https://github.com/noctalia-dev/noctalia-greeter/blob/main/examples/greeter.toml) as the canonical reference. Copy it to `/var/lib/noctalia-greeter/greeter.toml`, then remove the values you do not want to override.

Restart greetd after changing startup, output, input, or idle settings. Appearance and picker state are otherwise read the next time the greeter starts.
