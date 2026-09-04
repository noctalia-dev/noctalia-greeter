---
title: Displays
description: Configure monitor selection, layout, modes, transforms, scaling, and idle blanking.
sidebar:
  order: 4
---

# Displays

Noctalia Greeter uses its bundled wlroots compositor to configure displays. Put
the settings on this page under `[output]` in
`/var/lib/noctalia-greeter/greeter.toml`. On NixOS, use
`services.displayManager.noctalia-greeter.settings.output` with the nixpkgs
module or `programs.noctalia-greeter.settings.output` with the project flake.

- [Find connector names](#find-connector-names)
- [Choose which monitors show the greeter](#choose-which-monitors-show-the-greeter)
- [Arrange multiple monitors](#arrange-multiple-monitors)
- [Match the desktop output mode](#match-the-desktop-output-mode)
- [Rotate an output](#rotate-an-output)
- [Scale the interface](#scale-the-interface)
- [Blank displays when idle](#blank-displays-when-idle)
- [Apply changes](#apply-changes)

## Find connector names

Run this from a graphical Wayland session:

```sh
noctalia-greeter outputs
```

Use the connector names it prints, such as `DP-1` or `HDMI-A-1`, in the
settings below.

## Choose which monitors show the greeter

By default, the greeter shows the same login interface on every connected
monitor. Each display uses its own resolution and scale.

To show it on only one monitor, pin that connector:

```toml
[output]
name = "DP-2"
```

The compositor disables all other connectors at the KMS level while the
greeter is running. If `name` is empty, missing, invalid, or refers to a
disconnected monitor, the greeter falls back to all connected outputs.

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
between monitors. When `layout` is set but a connector has no matching scale,
that connector uses scale `1.0` instead of automatic DPI scaling. A global
`scale` setting overrides all per-output scales.

Noctalia can copy layout, transform, and effective scale values from the
desktop session through xdg-output. It records a layout only when multiple
ready outputs report distinct positions; transforms and scales need at least
one ready output. See [Sync with Noctalia](sync.md). Values declared in
`greeter.toml` take precedence over synchronized values in `sync.toml`.

## Match the desktop output mode

By default, the compositor uses each display's EDID-preferred resolution and
then selects the highest advertised refresh rate at that size. If this differs
from the desktop session, the display may flash or modeset during login.

Set both `width` and `height` to request a particular resolution:

```toml
[output]
name = "DP-2"
width = 5120
height = 2160
```

Both values are required and must be positive. A partial or invalid override
is ignored. If the display does not advertise the requested size, the
compositor logs a warning and falls back to the preferred-resolution behavior.
At the requested size, it still chooses the highest advertised refresh rate.

:::note
`width` and `height` select the physical DRM mode in pixels. They are separate
from `scale`, which changes the size of the greeter interface. Mode dimensions
are selected before any output rotation is applied.
:::

Match the resolution to the desktop session to avoid an unnecessary resolution
change when logging in.

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
2. A connector entry in `[output].scales` from `greeter.toml`, then `sync.toml`
3. Scale `1.0` when `[output].layout` applies but the connector has no scale
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

On NixOS with the nixpkgs module:

```nix
services.displayManager.noctalia-greeter.settings = {
  idle.timeout = 300;
};
```

With the project flake module instead:

```nix
programs.noctalia-greeter.settings = {
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
