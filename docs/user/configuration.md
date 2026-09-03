# Configuration

Configure **everything** in `/var/lib/noctalia-greeter/greeter.toml` (Nix: `programs.noctalia-greeter.settings`, overwritten with tmpfiles `C+`). Sync and the login UI never write that file.

| Path | Role |
|------|------|
| `/var/lib/noctalia-greeter/greeter.toml` | Full declarative config (scheme, palette, wallpaper, output, cursor, …). Wins over Sync when set |
| `/var/lib/noctalia-greeter/sync.toml` | Sync + UI mutable (palette, wallpaper refs, session actions, last session/scheme, layout/transforms/scales). Loses to greeter.toml |
| `/var/lib/noctalia-greeter/wallpaper*` | Sync-installed wallpaper image files |

The **Synced** scheme uses a complete `[appearance.palette]` from `greeter.toml` when present, otherwise the same keys from `sync.toml`. (Legacy live `appearance.json` is migrated into `sync.toml` once.)

The `[appearance].hide_*` keys are declarative `greeter.toml` settings. They are not read from or written to `sync.toml`.

If `greeter.toml` is missing, the greeter uses built-in defaults. Setup ensures `/var/lib/noctalia-greeter/` exists and is owned by the greetd session user.

- [Keys the greeter remembers](#keys-the-greeter-remembers)
- [Declarative greeter.toml keys](#declarative-greetertoml-keys)
- [Full greeter.toml example](#full-greetertoml-example)
- [Multi-monitor](#multi-monitor)
- [Output mode](#output-mode)
- [Output transform](#output-transform)
- [Synced wallpapers](#synced-wallpapers)
- [Idle blanking](#idle-blanking)
- [UI scale](#ui-scale)
- [Cursor theme](#cursor-theme)
- [Keyboard layout](#keyboard-layout)
- [Helper commands](#helper-commands)

---

## Keys the greeter remembers

When you change session or color scheme on the login screen, the greeter writes **`sync.toml`** (unless `greeter.toml` already pins `[appearance].scheme`):

| Key | Purpose |
|-----|---------|
| `[session].last` | Last Wayland session you picked (by **Name** from the session picker) |
| `[appearance].scheme` | Last color scheme you picked |

Sync stages a `sync.toml` fragment (and optional layout/transforms/scales files); apply merges that into live `sync.toml`.

---

## Declarative greeter.toml keys

Set these in **`greeter.toml`**. The greeter UI and Sync do not change them. When the same key exists in `sync.toml`, **greeter.toml wins**.

| Key | Purpose |
|-----|---------|
| `[session].default` | Session selected when the greeter opens. Overrides last-used `[session].last` unless you pass `--session` on the greetd command line. |
| `[user].default` | Username to select on startup. Opens the password step immediately. `--user` on the greetd command line wins. |
| `[appearance].scheme` | Color scheme (`Synced`, builtin name such as `Noctalia`, …). Overrides last UI pick in `sync.toml`. |
| `[appearance].password_style` | Password mask: `default` or `random` |
| `[appearance].hide_logo` | Hide the Noctalia brand logo (`true` / `false`) |
| `[appearance].hide_session_selector` | Hide the session selector (`true` / `false`; default `false`) |
| `[appearance].hide_scheme_selector` | Hide the scheme selector (`true` / `false`; default `false`) |
| `[appearance].hide_shutdown_button` | Hide the shutdown button (`true` / `false`; default `false`) |
| `[appearance].hide_reboot_button` | Hide the reboot button (`true` / `false`; default `false`) |
| `[appearance].hide_firmware_button` | Hide the firmware button (`true` / `false`; default `false`) |
| `[appearance].power_buttons_position` | Power buttons: `bottom-right` (default), `bottom-left`, `top-left`, `top-right`, `hidden` |
| `[appearance].scheme_selector_position` | Scheme selector: `top-right` (default), `top-left`, `bottom-left`, `bottom-right`, `hidden` |
| `[appearance].theme_mode` | Theme mode string for Synced look (e.g. `dark`) |
| `[appearance].corner_radius_scale` | Corner radius scale for Synced look |
| `[appearance].font_family` | Font family for Synced look |
| `[appearance.palette]` | Full palette (same keys Sync writes under `[appearance.palette]`). When complete, wins over Sync for the Synced scheme |
| `[appearance.wallpaper]` | Default wallpaper `path` / `fill_mode` / `fill_color` |
| `[appearance.wallpapers.<connector>]` | Per-output wallpaper override |
| `[output].name` | Wayland connector to pin the greeter on |
| `[output].layout` | Multi-monitor positions; overrides Sync layout in `sync.toml` |
| `[output].width` / `.height` | Preferred DRM mode size |
| `[output].transforms` | Per-connector DRM transform; overrides Sync transforms |
| `[output].scales` | Per-connector scale matching the session; overrides Sync scales |
| `[output].scale` | Manual UI scale for **all** outputs (overrides `scales` and auto) |
| `[idle].timeout` | Seconds before blanking outputs; `0` disables |
| `[cursor].theme` / `.size` / `.path` | Cursor theme |
| `[keyboard].layout` / `.variant` / `.options` / `.numlock` | XKB keymap |
| `[auth].allow_empty_password` | Empty password submit for fprintd/smartcard PAM |
| `[auth].request_timeout` | Seconds to wait for each greetd reply (`0`-`3600`); `0` disables the watchdog (default `60`) |

---

## Full greeter.toml example

Copy to `/var/lib/noctalia-greeter/greeter.toml` and drop any lines you do not need. Same file in the greeter repo: [`examples/greeter.toml`](https://github.com/noctalia-dev/noctalia-greeter/blob/main/examples/greeter.toml).

```toml
# noctalia-greeter - full greeter.toml example (declarative / Nix-safe)
#
# Install as /var/lib/noctalia-greeter/greeter.toml (owned by the greetd user).
# On NixOS: programs.noctalia-greeter.settings = { ... };  (tmpfiles C+ overwrite)
#
# This file is the full admin config. Sync and the login UI never write it.
# Mutable Sync data lives in sync.toml (lower priority when both set).
# Sync merges palette/wallpaper/session into sync.toml; a complete [appearance.palette] here wins.
#
# Omit any key for the built-in default.
# Docs: https://docs.noctalia.dev/greeter/configuration/

[session]
# Exact Name= from the session .desktop (same as `noctalia-greeter sessions` / the picker).
# Not the .desktop basename — e.g. "Hyprland (uwsm-managed)", not "hyprland-uwsm".
default = "niri"

[user]
# Opens the password step for this account on startup.
default = "lysec"

[appearance]
# Color scheme name: "Synced" (palette below or Sync sync.toml), or a builtin
# like "Noctalia", "Catppuccin", .... Overrides last UI pick in sync.toml when set.
scheme = "Synced"
# Password mask: "default" (filled circles) or "random" (cycled glyph shapes).
password_style = "random"
# Hide the Noctalia brand logo on the login screen.
hide_logo = false
# Hide individual controls. Omitted values default to false.
hide_session_selector = false
hide_scheme_selector = false
hide_shutdown_button = false
hide_reboot_button = false
hide_firmware_button = false
# Power buttons position: "bottom-right" (default), "bottom-left", "top-left", "top-right", "hidden"
power_buttons_position = "bottom-right"
# Scheme selector position: "top-right" (default), "top-left", "bottom-left", "bottom-right", "hidden"
scheme_selector_position = "top-right"
theme_mode = "dark"
corner_radius_scale = 1.0
# Optional; empty / omit keeps the greeter default font.
font_family = "Inter"

# Required for a declarative Synced look (same keys Sync writes under [appearance.palette]).
# When complete, this wins over Sync's sync.toml appearance.
[appearance.palette]
primary = "#fff59b"
on_primary = "#0e0e43"
secondary = "#a9aefe"
on_secondary = "#0e0e43"
tertiary = "#9BFECE"
on_tertiary = "#0e0e43"
error = "#FD4663"
on_error = "#0e0e43"
surface = "#070722"
on_surface = "#f3edf7"
surface_variant = "#11112d"
on_surface_variant = "#7c80b4"
outline = "#21215F"
shadow = "#070722"
hover = "#9BFECE"
on_hover = "#0e0e43"

[appearance.wallpaper]
# Absolute path, or color:#RRGGBB. fill_mode: center | crop | fit | stretch | repeat
path = "/var/lib/noctalia-greeter/wallpaper.webp"
fill_mode = "crop"
# fill_color = "#070722"

# Optional per-connector wallpapers (overrides [appearance.wallpaper] for that output):
# [appearance.wallpapers.DP-1]
# path = "/var/lib/noctalia-greeter/wallpaper-DP-1.webp"
# fill_mode = "crop"

[output]
# Pin the greeter to one connector; omit to mirror on every monitor.
# List names with: noctalia-greeter outputs
name = "DP-2"
# Multi-monitor positions (logical pixels). Overrides Sync layout in sync.toml when set.
layout = "DP-1:0,0; DP-2:2560,0"
# Preferred DRM mode size in pixels (both required if set).
width = 5120
height = 2160
# Per-connector DRM transform. Overrides Sync transforms in sync.toml when set.
# Tokens: normal/0/none, 90, 180, 270, flipped, flipped-90, flipped-180, flipped-270
transforms = "DP-1:normal; DP-2:normal"
# Per-connector scale matching the session (keeps layout coords valid). Overrides Sync scales when set.
scales = "DP-1:1; DP-2:1"
# Manual UI scale for all outputs; omit -> per-output scales, else auto from display geometry.
scale = 1.5

[idle]
# Seconds with no input before blanking outputs; 0 disables (range 0-86400).
timeout = 300

[cursor]
theme = "Adwaita"
size = 24
# Colon-separated search path when the theme is not under the default icon dirs.
path = "/usr/share/icons"

[keyboard]
# Comma-separated for multiple layouts.
layout = "us,cz"
variant = ",qwertz"
options = "grp:alt_shift_toggle"
# Start with Num Lock locked (default true if omitted).
numlock = true

[auth]
# Allow empty password submit (fprintd / smartcard PAM). Default false.
allow_empty_password = false
# Fail closed if greetd leaves a request unanswered (0-3600 seconds; 0 disables).
request_timeout = 60
```

---

## Multi-monitor

The greeter runs inside the bundled wlroots compositor (`noctalia-greeter-compositor`). By default it shows the **same login UI on every connected monitor**, with each display sized to its own resolution and scale.

To pin the greeter to a single connector:

```toml
[output]
name = "DP-2"
```

When `[output].name` is set, the compositor disables the other connectors at the KMS level. If it is missing, empty, or names a disconnected connector, the greeter falls back to showing on all outputs.

On multiple monitors, cursor movement follows `[output].layout`. Without it, the greeter places outputs left-to-right by connector name, which often does not match your desk.

**Sync from Noctalia:** When you use **Settings → Security → Noctalia Greeter → Sync Now**, Noctalia copies monitor positions from your desktop compositor (via xdg-output) into `[output].layout`, each connector's transform into `[output].transforms`, and each connector's effective scale into `[output].scales`. Sync skips layout when only one monitor is connected, xdg-output is unavailable, outputs are still enumerating, or all monitors report the same origin. Transforms and scales sync whenever at least one ready output is available.

Set positions manually if needed:

```toml
[output]
layout = "DP-1:0,0; DP-2:2560,0"
scales = "DP-1:1; DP-2:1"
```

Coordinates are **logical pixels** from your desktop compositor. The greeter applies matching `[output].scales` so those positions stay adjacent and the cursor can move between monitors. Without `scales`, a configured `layout` uses scale `1.0` (not auto DPI scale) so absolute positions do not open gaps. A global `[output].scale` overrides per-output scales.

List connector names from a running Wayland session:

```sh
noctalia-greeter outputs
```

Restart greetd after changing `[output].name` or `[output].width` / `[output].height`:

```sh
sudo systemctl restart greetd
```

---

## Output mode

By default the greeter compositor modesets each connector using the EDID preferred resolution, then the **highest refresh rate** available at that size. On some setups that mode differs from your desktop session (resolution and/or refresh), so login flashes or modesets when the session starts.

To pin the greeter to a specific resolution, set both `[output].width` and `[output].height`:

```toml
[output]
name = "DP-2"
width = 5120
height = 2160
```

When both values are set, the compositor uses that size and still picks the **highest-refresh** advertised mode for it. If no mode of that size is advertised, it logs a warning and falls back to the preferred-resolution path above.

Both keys are required. If only one is set, the greeter ignores the partial override and uses the preferred-resolution path. Invalid (non-positive) values are also ignored.

:::note
`[output].width` / `[output].height` control the **physical DRM mode** (pixels). They are separate from `[output].scale`, which only affects greeter UI scaling.
:::

Match these values to the mode your desktop session uses if you want a seamless greeter → session transition.

---

## Output transform

Portrait panels often need a DRM transform so the greeter UI is upright. Set `[output].transforms` to a semicolon-separated list of `CONNECTOR:TOKEN` entries:

```toml
[output]
# optional: pin greeter to the portrait panel only
# name = "HDMI-A-1"
transforms = "HDMI-A-1:270"
```

Supported tokens: `normal` / `0` / `none`, `90`, `180`, `270`, `flipped`, `flipped-90`, `flipped-180`, `flipped-270`.

Multiple connectors:

```toml
[output]
transforms = "DP-1:normal; HDMI-A-1:90"
```

:::note
Transform is independent of `[output].width` / `[output].height` (those select the physical DRM mode size before rotation) and of `[output].scale` (UI scale).
:::

Noctalia **Sync Now** also writes `[output].transforms` into `sync.toml` from your desktop compositor's reported output transforms (including `normal`). Setting `[output].transforms` in `greeter.toml` (or via your Nix module) overrides Sync. Restart greetd after changes:

```sh
sudo systemctl restart greetd
```

List connector names from a running Wayland session:

```sh
noctalia-greeter outputs
```

---

## Synced wallpapers

**Settings → Security → Noctalia Greeter → Sync Now** installs wallpaper image files under `/var/lib/noctalia-greeter/` and merges wallpaper references into **`sync.toml`** (not into declarative `greeter.toml`).

With a current Noctalia shell and greeter:

| On disk / in config | Purpose |
|---------------------|---------|
| `wallpaper` / `wallpaper.<ext>` + `[appearance.wallpaper]` in `sync.toml` | Default **single** image (always written by Sync as a fallback) |
| `wallpaper-<connector>.*` + `[appearance.wallpapers.<connector>]` in `sync.toml` | **Per-connector** map (`DP-2`, `HDMI-A-1`, …) |

Each greeter view picks the image for its bound connector name. If that connector has no entry, it uses the single `[appearance.wallpaper]` fallback.

If you pin the greeter with `[output].name = "DP-2"`, you see the DP-2 wallpaper when that map entry exists (and only that connector is shown).

You do **not** need extra `greeter.toml` keys for Sync wallpapers — only Sync Now (and optional pin as above). To set wallpapers declaratively instead of (or on top of) Sync, use the same keys in `greeter.toml`:

```toml
[appearance.wallpaper]
path = "/var/lib/noctalia-greeter/wallpaper.webp"
fill_mode = "crop"

[appearance.wallpapers.DP-2]
path = "/var/lib/noctalia-greeter/wallpaper-DP-2.webp"
fill_mode = "crop"
```

When `greeter.toml` provides a **complete** `[appearance.palette]` (declarative Synced look), that whole appearance — including wallpaper keys — is used and Sync's `sync.toml` appearance is not. Set wallpaper tables in `greeter.toml` in that case, or omit the complete palette so Sync's wallpapers apply.

Legacy live `appearance.json` is migrated into `sync.toml` once if present; Sync no longer leaves a live `appearance.json` as the source of truth.

---

## Idle blanking

By default the greeter never blanks the screen. To turn off active DRM outputs after a period with no input, set `[idle].timeout` in seconds:

```toml
[idle]
timeout = 300
```

`0` or omitting the key disables blanking. Valid values are `0`-`86400` (24 hours).

While blanked, the greeter client stays running. Any **key press**, **mouse button press**, or **touch** wakes the displays and restarts the idle timer. Pointer motion and scroll only wake a blanked screen - they do not reset the timer while the screen is on (wireless mice often emit motion noise that would otherwise prevent blanking).

Optional environment override (wins when set), useful because greetd starts greeters with an empty environment. Put it on the greetd session **command** (not only as a bare shell assignment):

```toml
[default_session]
command = "env NOCTALIA_GREETER_IDLE_TIMEOUT=300 /usr/bin/noctalia-greeter-session"
```

On NixOS, set the same value through the module (it is written into `greeter.toml`; the module does not wrap the greetd command with `NOCTALIA_GREETER_IDLE_TIMEOUT`):

```nix
programs.noctalia-greeter.settings = {
  idle.timeout = 300;
};
```

Restart greetd after changing the timeout:

```sh
sudo systemctl restart greetd
```

---

## UI scale

On high-DPI panels (for example 4K without fractional scaling), and when no layout/`scales`/`scale` override applies, the compositor scales output from the monitor's physical size when EDID reports it, otherwise from resolution. Auto scale uses the **selected DRM mode** size (not the pre-modeset `wlr_output` size), so cold boot and post-logout match. Auto scale is capped at 2×. The greeter client lays out at logical size and renders HiDPI buffers via Wayland fractional scale.

Resolution order per output:

1. Global `[output].scale` in `greeter.toml` (forces one scale on every output)
2. Per-connector `[output].scales` from `greeter.toml`, else Sync `sync.toml`
3. If `[output].layout` is set but no matching scales → scale `1.0` (keeps session layout coords adjacent)
4. Otherwise auto from display geometry

Per-connector scales (synced from the desktop session):

```toml
[output]
scales = "DP-1:1; DP-2:1.25"
```

Force one scale on every output:

```toml
[output]
scale = 1.5
```

If `[output].scale` is set but invalid (not a positive number), it is ignored and the next step in the order above applies.

:::note
Manual `[output].scale` and Sync `[output].scales` only affect the greeter session. They are separate from your desktop compositor's scaling, except that **Sync Now** copies the session's effective per-output scale into `scales` so multi-monitor layout stays valid.
:::
---

## Cursor theme

The compositor resolves the cursor theme, size, and search path in this order:

1. `[cursor].theme` / `[cursor].size` / `[cursor].path` in `greeter.toml`
2. The `XCURSOR_THEME`, `XCURSOR_SIZE`, and `XCURSOR_PATH` environment variables
3. The wlroots defaults (built-in cursor at size `24`)

Set the keys in `greeter.toml`:

```toml
[cursor]
theme = "Adwaita"
size = 24
```

If the theme is not under the default search path (`~/.icons:/usr/share/icons:/usr/share/pixmaps`), also set `[cursor].path` to the directory that contains it:

```toml
[cursor]
path = "/usr/share/icons"
```

### Using environment variables

greetd starts greeters with an empty environment, so the `XCURSOR_*` variables must be set in the greetd session **command** rather than the service environment, for example in `/etc/greetd/config.toml`:

```toml
[default_session]
command = "env XCURSOR_THEME=Adwaita XCURSOR_SIZE=24 /usr/bin/noctalia-greeter-session"
```

### On NixOS

The module option `programs.noctalia-greeter.settings` writes `/var/lib/noctalia-greeter/greeter.toml` (Nix attrset, TOML string, or path). Cursor keys become `[cursor]` in that file. The module does **not** inject `XCURSOR_*` into the greetd session command and there is no `package` option. Point `path` at the theme package's `share/icons` instead:

```nix
programs.noctalia-greeter.settings = {
  cursor = {
    theme = "Bibata-Modern-Ice";
    size = 24;
    path = "${pkgs.bibata-cursors}/share/icons";
  };
};
```

| Key | Written to |
|-----|------------|
| `theme` | `[cursor].theme` |
| `size` | `[cursor].size` |
| `path` | `[cursor].path` (compositor sets `XCURSOR_PATH` from this) |

Prefer `settings.cursor` over wrapping the greetd command with `env XCURSOR_*` when using the module.

---

## Keyboard layout

The compositor loads the XKB keymap in this order:

1. `[keyboard].layout` / `[keyboard].variant` / `[keyboard].options` in `greeter.toml`
2. The `XKB_DEFAULT_LAYOUT`, `XKB_DEFAULT_VARIANT`, and `XKB_DEFAULT_OPTIONS` environment variables
3. The system default keymap

Example for Czech QWERTZ:

```toml
[keyboard]
layout = "cz"
```

Multiple layouts (cycle with `[keyboard].options`, e.g. `grp:alt_shift_toggle`):

```toml
[keyboard]
layout = "us,cz"
options = "grp:alt_shift_toggle"
```

Use standard [XKB layout codes](https://man.archlinux.org/man/xkeyboard-config.7#LAYOUTS) (`de`, `fr`, `ru`, ...). List layouts on your system with `localectl list-x11-keymap-layouts` or check `/usr/share/X11/xkb/rules/base.lst`.

### Num Lock

The compositor locks Num Lock on startup so numeric keypads work without extra setup. If your keyboard misbehaves with Num Lock enabled (for example, the `0` digit key producing incorrect characters), disable it:

```toml
[keyboard]
numlock = false
```

The default is `true` (Num Lock locked). This setting has no effect when Num Lock is not available on the keymap.

greetd starts greeters with an empty environment, so set layout in `greeter.toml` or prefix the greetd session command:

```toml
[default_session]
command = "env XKB_DEFAULT_LAYOUT=cz /usr/bin/noctalia-greeter-session"
```

---

## Helper commands

```sh
noctalia-greeter sessions   # list Name= values for [session].default / --session (not .desktop basenames)
noctalia-greeter outputs    # list Wayland connector names for [output].name
```

Sessions come from `wayland-sessions` `.desktop` files under `/usr/share`, each path in `XDG_DATA_DIRS`, and on NixOS `/run/current-system/sw/share`. Matching uses each entry's **`Name=`** field (case-insensitive), which is what the picker displays — not the file stem such as `hyprland-uwsm.desktop`.
