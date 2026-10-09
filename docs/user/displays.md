---
title: Displays
description: Configure monitor selection, layout, modes, transforms, scaling, and idle blanking.
sidebar:
  order: 4
---

Noctalia Greeter uses its bundled wlroots compositor to configure displays. Put
the settings on this page under `[output]` in
`/var/lib/noctalia-greeter/greeter.toml`. On NixOS, use
`services.displayManager.noctalia-greeter.settings.output` with the nixpkgs
and project flake modules.

- [Find connector names](#find-connector-names)
- [Choose which monitors show the greeter](#choose-which-monitors-show-the-greeter)
- [Arrange multiple monitors](#arrange-multiple-monitors)
- [Match the desktop output mode](#match-the-desktop-output-mode)
- [Rotate an output](#rotate-an-output)
- [Scale the interface](#scale-the-interface)
- [Use automatic display settings with appearance sync](#use-automatic-display-settings-with-appearance-sync)
- [Blank displays when idle](#blank-displays-when-idle)
- [Apply changes](#apply-changes)

## Find connector names

Run this from a graphical Wayland session:

```sh
noctalia-greeter outputs --details
```

The first column is the connector name, such as `DP-1` or `HDMI-A-1`. When the
display exposes EDID identity data, the second column is a stable identifier
made from its manufacturer, model, and serial number. Either value can be used
for `[output].name` and for entries in `layout`, `transforms`, and `scales`.
Per-output wallpaper entries continue to use connector names.

## Choose which monitors show the greeter

By default, the greeter shows the same login interface on every connected
monitor. Each display uses its own resolution and scale.

On laptops, the compositor follows the libinput lid switch. While the lid is
closed and an external display can be enabled, internal `eDP-*` and `LVDS-*`
outputs are disabled and removed from the greeter layout. They return when the
lid opens or the last usable external display disappears. An explicit
`[output].name` pin takes precedence over automatic lid handling.

To show it on only one monitor, pin that connector:

```toml
[output]
name = "DP-2"
```

If connector numbers change across boots, use the stable identifier shown by
`noctalia-greeter outputs --details` instead:

```toml
[output]
name = "Acer Technologies XV242Y TL1EE0018521"
```

The compositor disables all other connectors at the KMS level while the
greeter is running. A non-selected connector that is already disabled remains
off without a temporary modeset. If `name` is empty, missing, invalid, or
refers to a disconnected monitor, the greeter falls back to all connected
outputs.

## Arrange multiple monitors

Without an explicit layout, outputs are placed from left to right in connector
name order. If that does not match the physical arrangement, set their logical
positions:

```toml
[output]
layout = "DP-1:0,0; DP-2:2560,0"
scales = "DP-1:1; DP-2:1"
```

`layout` coordinates are logical pixels. Use matching per-output `scales` so
the configured edges remain adjacent and the pointer can move naturally
between monitors. Entries may use connector names or stable identifiers. Use
semicolons between entries when identifiers contain spaces:

```toml
[output]
layout = "Dell Inc. DELL U2723QE ABC123:0,0; LG Electronics LG HDR 4K XYZ789:2560,0"
scales = "Dell Inc. DELL U2723QE ABC123:1.25; LG Electronics LG HDR 4K XYZ789:1.5"
```

When a configured layout entry has no matching scale, that output uses scale
`1.0` to preserve its recorded logical coordinates. Connected outputs omitted
from the layout are placed after the rightmost configured output and retain
automatic DPI scaling. A global `scale` setting overrides all per-output
scales.

Noctalia can copy layout, transform, and effective scale values from the
desktop session through xdg-output. It records a layout only when multiple
ready outputs report distinct positions; transforms and scales need at least
one ready output. See [Sync with Noctalia](sync.md). Values declared in
`greeter.toml` take precedence over synchronized values in `sync.toml`.

## Match the desktop output mode

By default, the compositor uses each display's complete EDID-preferred mode,
including its refresh rate. This avoids selecting a higher advertised rate that
a bandwidth-limited dock or MST link cannot drive.

Set both `width` and `height` to request a particular resolution:

```toml
[output]
name = "DP-2"
width = 5120
height = 2160
refresh_rate = 120
```

Both values are required and must be positive. A partial or invalid override
is ignored. `refresh_rate` is in hertz and can be set with or without an
explicit size. Without `width` and `height`, it applies to the EDID-preferred
resolution. Nominal rates match fractional DRM modes within 1 Hz, so `120`
matches modes such as `119.998`. If the display does not advertise the requested
size or a close refresh rate, the compositor logs a warning and falls back to
the highest advertised refresh rate at the selected resolution.

For multiple displays with different refresh rates, use the mapping form of the
same key:

```toml
[output]
refresh_rate = "DP-1:120; HDMI-A-1:60"
```

Entries are separated by semicolons and may use either connector names or the
stable identifiers reported by `noctalia-greeter outputs --details`. Use either
the numeric global form or the per-output mapping form, not both.

:::note
`width` and `height` select the physical DRM mode in pixels, while
`refresh_rate` selects refresh rates in hertz. It is separate from `scale`,
which changes the size of the greeter interface. Mode dimensions are selected
before any output rotation is applied.
:::

Match the resolution and refresh rate to the desktop session to avoid an
unnecessary mode change when logging in.

## Rotate an output

For a portrait or flipped display, assign a transform to its connector:

```toml
[output]
transforms = "HDMI-A-1:270"
```

Available tokens are `normal`, `0`, `none`, `90`, `180`, `270`, `flipped`,
`flipped-90`, `flipped-180`, and `flipped-270`. Separate multiple connectors
with semicolons:

```toml
[output]
transforms = "DP-1:normal; HDMI-A-1:90"
```

Transform is independent of the physical mode size and UI scale. Noctalia can
also synchronize transforms; see [Sync with Noctalia](sync.md).

## Scale the interface

When no layout or scale override applies, the greeter calculates each output's
scale from its physical dimensions reported by EDID, falling back to its
resolution. It uses the selected DRM mode size so cold boot and post-logout
results match. Automatic scale is capped at `2`.

Scale is resolved independently for each output in this order:

1. Global `[output].scale` in `greeter.toml`
2. A connector or stable-identifier entry in `[output].scales` from `greeter.toml`, then `sync.toml` when `[output].use_synced_settings` is `true` (the default)
3. Scale `1.0` when the output has a matching `[output].layout` entry but no scale
4. Automatic scale from display geometry

Set different scales per connector:

```toml
[output]
scales = "DP-1:1; DP-2:1.25"
```

Or force one scale on every output:

```toml
[output]
scale = 1.5
```

An invalid global scale, including a non-positive number, is ignored and
resolution continues with the next applicable step above. The greeter lays out
its interface at logical size and renders HiDPI buffers with Wayland fractional
scaling.

These settings affect only the greeter session. They do not configure scaling
inside the desktop session, although Noctalia Sync can copy the desktop's
effective per-output scales to keep synchronized layouts valid.

## Use automatic display settings with appearance sync

If you connect different monitors to the same connector, or switch a monitor
between resolution modes, a previously synced scale may no longer be suitable.
To use the greeter's display defaults while continuing to sync its appearance,
set this in `greeter.toml`:

```toml
[output]
use_synced_settings = false
```

The option defaults to `true`. With `false`, the greeter ignores the synced
`layout`, `transforms`, and `scales` in `sync.toml`. Output settings declared
in `greeter.toml` still apply, including a pinned monitor, mode overrides,
manual scales, and rotation.

For automatic layout and scaling, omit `layout`, `scales`, and `scale` from
`greeter.toml`. Outputs are then arranged from left to right and scaled from
their current display geometry. A declarative layout without a matching scale
still uses scale `1.0`, as described above; synced layout and scale are ignored
together because the layout positions were recorded in logical pixels.

Sync continues to update appearance and store output metadata in `sync.toml`.
The option controls whether the greeter uses that metadata, so it also applies
to previously saved settings and later sync operations. Wallpaper and palette
syncing continue normally. Restart greetd after changing the option.

## Blank displays when idle

By default, the greeter never blanks the screen. To turn off active DRM outputs
after a period without input, set `[idle].timeout` in seconds:

```toml
[idle]
timeout = 300
```

`0` or an omitted key disables blanking. Valid values are `0` through `86400`
(24 hours).

The greeter client keeps running while the outputs are blanked. A key press,
mouse button press, or touch wakes them and restarts the idle timer. Pointer
motion and scrolling wake an already blanked screen, but do not reset the timer
while the screen is on. This prevents motion noise from a wireless mouse from
keeping the greeter awake indefinitely.

`NOCTALIA_GREETER_IDLE_TIMEOUT` overrides the config value when set. Because
greetd starts the greeter with an empty environment, add the variable to its
session command with `env`:

```toml
[default_session]
command = "env NOCTALIA_GREETER_IDLE_TIMEOUT=300 /usr/bin/noctalia-greeter-session"
```

On NixOS with the nixpkgs and project flake modules:

```nix
services.displayManager.noctalia-greeter.settings = {
  idle.timeout = 300;
};
```

Both modules write the value to `greeter.toml`; neither adds the environment
override to the greetd command. Restart greetd after changing the timeout as
shown below.

## Apply changes

Restart greetd after changing display settings:

```sh
sudo systemctl restart greetd
```

On runit:

```sh
sudo sv restart greetd
```
