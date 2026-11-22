---
title: Configuration
description: Configure greeter.toml defaults and understand mutable sync.toml state.
sidebar:
  order: 2
---

Administrator-controlled settings live in `/var/lib/noctalia-greeter/greeter.toml`. Noctalia Sync and choices made on the login screen use the lower-priority `sync.toml`; they never rewrite `greeter.toml`.

- [Configuration files](#configuration-files)
- [Keys the greeter remembers](#keys-the-greeter-remembers)
- [Desktop widgets](#desktop-widgets)
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

## Desktop widgets

Both files accept validated `[widgets]` profiles for the greeter. An absent table leaves widgets unmanaged, an empty
table requests defaults, and `widget_order = []` explicitly stores an empty
collection. Explicit collections render as desktop widgets beneath the native login card,
chrome, error banner and menus. Preference and appearance updates preserve them. Invalid widget metadata is
reported with its field path and blocks writes to that file until repaired.

An explicit administrator `[widgets]` table owns the entire effective collection,
including a request for defaults or an empty list. Sync supplies the collection
only when that table is absent; collections are never merged by widget ID. If the
administrator collection or TOML is invalid, its diagnostic remains visible and
the greeter does not select Sync's collection instead. Repairing or removing the
administrator override takes effect on the next load. Session and scheme saves
preserve both stored collections.

Widgets load when the greeter starts. Eligible types are `label`, `clock`
(digital or analog), `sysmon` (graph or gauge), `button`, and `sticker`.
Sticker images load from the managed `widget-assets/` directory inside the
state directory. Use `image_path = "widget-assets/<lowercase-sha256>.png"` or
that file's exact installed absolute path. The hash is the SHA-256 of the PNG's
encoded bytes; names, hashes and content are checked before publication.
Multiple instances and outputs share decoded pixels and one texture upload,
including when one reference is relative and another is absolute.

Only PNG files with dimensions from 1 to 4096 on each axis are accepted, at most
16 MiB encoded per file and 32 distinct images. Aggregate limits are 64 MiB
encoded and 128 MiB decoded. Directories and files must be owned by root or the
running greeter user and must not be group/world writable. Symlink traversal,
hard-linked files, arbitrary paths, home expansion and URLs are rejected.
The [constrained sync helper](sync.md#customization-payload) can install staged
profiles and managed PNGs. Noctalia's customization Apply integration is still
pending; ordinary appearance sync preserves the existing widget collection.

An empty Sticker retains its placeholder. Missing, corrupt or invalid managed
images prevent the collection from mounting and show a profile error while
native login remains available. Assets load once at startup; restart the greeter
after changing the profile or installed files.

An absent table, a default request, or an empty collection adds no custom widgets
in this integration stage. The existing native clock remains separate until the
planned Clock-widget cutover; use `[clock].enabled = false` to hide it when
configuring your own clocks.

```toml
[widgets]
widget_order = ["welcome", "load"]

[widgets.widget.welcome]
type = "label"
placement = "anchor"
anchor = "top-left"
y = 100
box_width = 500
box_height = 100
[widgets.widget.welcome.settings]
title = "Welcome home"
background = false

[widgets.widget.load]
type = "sysmon"
placement = "anchor"
anchor = "bottom-left"
y = -64
box_width = 320
box_height = 120
[widgets.widget.load.settings]
stat = "cpu_usage"
stat2 = "ram_pct"
```

Widget order runs from back to front within the widget layer. Disabled widgets
and unmatched `output` connector/stable IDs are not mounted. Widgets outside the
view stop ticking and requesting metrics. Relative placement uses `cx`, `cy`,
`placement_width`, and `placement_height` instead of anchor/offset fields.
Rotation and axis flips use `rotation`, `flip_x`, and `flip_y`; zero
`box_width`/`box_height` requests natural size.

Sysmon supports CPU usage, CPU frequency, CPU temperature, RAM usage and swap
usage (`cpu_usage`, `cpu_freq`, `cpu_temp`, `ram_pct`, `swap_pct`). The application
samples the union of visible requests once per second and shares each snapshot
across outputs. A gauge requests only its primary metric. Unavailable readings
show `--`; validated zero-capacity swap shows `No swap`. No GPU metrics are
available. Unused metrics are not polled, and inactive views stop widget ticks
and demand.

Buttons use the bounded `action` setting (`none`, `power`, `appearance`,
`sessions`, `suspend`, `reboot`, `shutdown`, or `firmware`) through the existing
native greeter handlers; arbitrary commands are not accepted. Hidden,
inactive, or removed widgets cannot retain an action or its menu focus.

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

Session keys are read only from the desktop file's `[Desktop Entry]` group;
`[Desktop Action ...]` groups do not change its name, command or visibility.

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

The default UI places the clock and date on the left, the login card with the host name on the right, the Noctalia logo at the top left, and keyboard layout, Appearance, and Power at the bottom right. The login card has no drop shadow. Branding uses only the logo. Hide it with `[appearance].hide_logo = true` or `[layout.branding].visible = false`; an explicit component visibility setting takes precedence over `hide_logo`. It uses the current Noctalia default palette. The keyboard indicator shows the active compositor keymap and is read-only; its default width follows the label so it sits beside Appearance. Appearance and Power share the same button styling and vertical alignment. Power opens a menu; selecting suspend, restart, shutdown, or firmware restart executes it immediately without a second confirmation.

When both a default user and a discovered default session are configured (through CLI or `greeter.toml`), the card automatically uses the compact presentation: identity, credential field, and Sign in. A remembered last session alone does not enable it. Missing session defaults use the full card. Press **Esc** to choose another account or session; selecting a different account/session restores the full card.

Both `greeter.toml` and `sync.toml` retain component layout settings. Each explicit administrator field wins over the corresponding Sync field; unset fields inherit Sync, then built-in defaults. An empty `[layout]` table leaves fields unspecified. The [constrained sync helper](sync.md#customization-payload) can replace or reset Sync's component layout. Ordinary UI preference saves and appearance-only updates preserve stored Sync layouts.

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

`login_mode` accepts `auto` (default; an explicit `auto` overrides a synced mode), `full` (always show pickers), or `minimal` (use the compact card when valid defaults are available, otherwise show the full card). Component IDs are `login`, `clock`, `branding`, `power`, `appearance`, and `keyboard`. Settings are partial overrides of built-in defaults, resolved by ID; there are no component lists or removal operations. Hide an optional component with `visible = false`.

| Field | Values and behavior |
| --- | --- |
| `visible` | Boolean. Login is required: `false` is rejected for `login`. |
| `anchor` | `top-left`, `top-center`, `top-right`, `center-left`, `center`, `center-right`, `bottom-left`, `bottom-center`, `bottom-right`. |
| `x`, `y` | Finite logical-pixel offsets from the anchor, from -8192 to 8192. Final placement is clamped inside the output. |
| `width` | Finite logical pixels, from 32 to 1600; login width is further bounded to 280–640 and available space. |
| `output` | Optional exact connector name or stable monitor identifier. An absent/disconnected target hides that optional component. Rejected for `login`, which remains on every active greeter output. |
| `rotation` | Finite radians, normalized to [-π, π), around the component center. |
| `scale` | Finite number from 0.5 to 4. Uniformly scales the whole component, including input. Reduced as needed to keep its rotated bounds inside the output. |
| `flip_x`, `flip_y` | Booleans. Flip the whole component horizontally or vertically, including input. |

Explicit administrator component fields override corresponding legacy `[clock]` and `[appearance]` keys. Explicit legacy administrator keys also win over matching Sync fields; legacy defaults do not mask Sync. Removing an override restores the next available value. `above-panel` positions the clock above the card when there is room. Unknown IDs/fields, wrong types, invalid anchors, and out-of-range/non-finite values are ignored individually and reported in the startup log and configuration banner. Valid administrator sibling fields remain active, but an invalid administrator layout blocks inheritance from Sync until repaired or removed. An invalid Sync layout is ignored as a whole. Writers refuse to replace invalid files. No layout version field is currently required; unsupported keys are ignored with diagnostics.

Placements use output-local logical coordinates. At desktop sizes the card and clock use matching outer gutters (6% of width, bounded to 64–128 logical pixels). Branding and footer use 48-pixel gutters. Widths below 1000 or heights below 600 use the narrow composition with smaller margins. Login keeps priority, followed by clock, branding, Power, Appearance, and keyboard; collision checks use rotated and scaled bounds, and colliding optional components move inward or disappear. Layout reserves space for configuration errors. Very small views uniformly scale a minimum 320×520 logical canvas, including hit testing, so credential controls stay reachable. Each view calculates its own geometry; fractional/mixed output scaling does not mutate global UI metrics. Configured fonts, palettes, and wallpapers still use the existing appearance rules.

Power and Appearance popups align with the right edge of their transformed trigger bounds, separated by 10 logical pixels. Account and session pickers align with the left edge of their transformed trigger bounds. Popups stay upright and keep their normal size when a trigger is scaled, rotated, or flipped. Popups switch above or below their trigger when space requires it and stay within the view, including when components are moved to the top of the screen.

## Clock

The clock is enabled by default at the left of the screen, with the date directly below the time. Both use the active palette's `on_surface` color, including when the **Synced** scheme is selected. The date uses medium-weight text and a subtle shadow over wallpapers. Spacing follows the visible text bounds so font line padding does not leave an oversized gap. The host name appears in the login card's subtitle, including with the compact card or when the clock is hidden.

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
