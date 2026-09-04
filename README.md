Noctalia Greeter
===

A minimal login greeter for [greetd](https://github.com/kennylevinsen/greetd) that matches the look and feel of [Noctalia Shell](https://github.com/noctalia-dev/noctalia-shell).

<p><br/></p>

<p align="center">
  <img src="https://assets.noctalia.dev/noctalia-logo.svg?v=2" alt="Noctalia Logo" style="width: 192px" />
</p>

<p><br/></p>

<p align="center">
  <a href="https://github.com/noctalia-dev/noctalia-greeter/commits">
    <img src="https://img.shields.io/github/last-commit/noctalia-dev/noctalia-greeter?style=for-the-badge&labelColor=0C0D11&color=A8AEFF&logo=git&logoColor=FFFFFF&label=commit" alt="Last commit" />
  </a>
  <a href="https://github.com/noctalia-dev/noctalia-greeter/stargazers">
    <img src="https://img.shields.io/github/stars/noctalia-dev/noctalia-greeter?style=for-the-badge&labelColor=0C0D11&color=A8AEFF&logo=github&logoColor=FFFFFF" alt="GitHub stars" />
  </a>
  <a href="https://docs.noctalia.dev">
    <img src="https://img.shields.io/badge/docs-A8AEFF?style=for-the-badge&logo=gitbook&logoColor=FFFFFF&labelColor=0C0D11" alt="Documentation" />
  </a>
  <a href="https://discord.noctalia.dev">
    <img src="https://img.shields.io/badge/discord-A8AEFF?style=for-the-badge&labelColor=0C0D11&logo=discord&logoColor=FFFFFF" alt="Discord" />
  </a>
</p>

## What is Noctalia Greeter?

Noctalia Greeter is the screen you see before your desktop session starts. It lets you pick a user, enter your password, choose a Wayland session, and pick a color scheme - with the same visual language as Noctalia Shell.

It is built for **greetd**: greetd starts the bundled wlroots compositor (`noctalia-greeter-compositor`), and the greeter runs inside that session.

Pair it with **[Noctalia v5](https://github.com/noctalia-dev/noctalia)** if you want wallpaper and palette synced from the shell settings (optional).

## Dependencies

Install everything below on the machine where greetd will run. Each list covers build tools and libraries, plus **greetd** and **D-Bus** (used by `noctalia-greeter-session`). You still need your desktop sessions separately (niri, Hyprland, and so on).

### Arch

```sh
sudo pacman -S meson gcc just \
  greetd dbus \
  wayland wayland-protocols wlroots0.20 \
  libglvnd freetype2 fontconfig \
  cairo pango harfbuzz \
  libxkbcommon glib2 \
  tomlplusplus nlohmann-json stb \
  libwebp librsvg libxml2
```

### Fedora

```sh
sudo dnf install meson gcc-c++ just \
  greetd dbus \
  wayland-devel wayland-protocols-devel wlroots-devel \
  libEGL-devel mesa-libGLES-devel \
  freetype-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  tomlplusplus-devel json-devel stb_image_resize2-devel \
  libwebp-devel librsvg2-devel libxml2-devel
```

### openSUSE Tumbleweed / Slowroll

```sh
sudo zypper install meson gcc-c++ just \
  greetd dbus-1 \
  wayland-devel wayland-protocols-devel wlroots-devel \
  Mesa-libEGL-devel Mesa-libGLESv2-devel \
  freetype2-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  tomlplusplus-devel nlohmann_json-devel stb-devel \
  libwebp-devel librsvg-devel libxml2-devel
```

### Debian / Ubuntu

```sh
sudo apt install meson g++ just \
  greetd dbus \
  libwayland-dev wayland-protocols libwlroots-0.20-dev \
  libegl-dev libgles-dev \
  libfreetype-dev libfontconfig-dev \
  libcairo2-dev libpango1.0-dev libharfbuzz-dev \
  libxkbcommon-dev libglib2.0-dev \
  libtomlplusplus-dev nlohmann-json3-dev libstb-dev \
  libwebp-dev librsvg2-dev libxml2-dev
```

### Void Linux

```sh
sudo xbps-install meson ninja pkg-config git \
  greetd dbus \
  wayland-devel wayland-protocols wlroots-devel libepoxy-devel \
  MesaLib-devel libglvnd-devel cairo-devel \
  pango-devel fontconfig-devel freetype-devel harfbuzz-devel \
  tomlplusplus-devel nlohmann-json-devel stb \
  libxkbcommon-devel libwebp-devel librsvg-devel libxml2-devel
```

Vendored dependencies, with no system package needed: `Wuffs`.

Dependencies that are vendored by default, with a meson option to
instead use the system package: `tomlplusplus`

Build requires `wlroots-0.20` and `wayland-server` development packages (see distro lists above).

`libwebp` handles WebP wallpapers when syncing appearance from the shell. Wuffs handles other raster image formats.

On Void Linux, `libepoxy-devel` is used when EGL/GLES pkg-config modules are not available.

Polkit and `pkexec` are optional for login, but required for appearance sync
from Noctalia. Passwordless sync additionally requires the packaged constrained
Polkit action.

## Installation

### Distribution packages

Install `noctalia-greeter` from your distribution when available. A complete
package provides the greeter, session wrapper, apply helper, assets, and the
optional Polkit action used for appearance sync. The
**[installation guide](https://docs.noctalia.dev/greeter/installation/)** has
package-specific commands for Arch Linux, CachyOS, KaOS, Fedora, Debian, and
Ubuntu, as well as the manual route for other distributions.

After installation, point greetd at the packaged
`noctalia-greeter-session` unless the package already did so. See
[Setting up greetd](#setting-up-greetd).

### NixOS

A flake and NixOS module are provided.

Add the input to your `flake.nix`:

```nix
inputs = {
  noctalia-greeter = {
    url = "github:noctalia-dev/noctalia-greeter";
    inputs.nixpkgs.follows = "nixpkgs";
  };
};
```

Import the module and enable the greeter in your configuration:

```nix
imports = [
  inputs.noctalia-greeter.nixosModules.default
];

programs.noctalia-greeter = {
  enable = true;

  # Optional configuration
  greeter-args = "";
  # Optional and empty by default: allow selected active local users to sync
  # appearance without a password. Omit this to keep an admin prompt every time.
  passwordless-sync-users = [ "alice" ];
  # Full declarative greeter.toml (overwritten on each activation).
  # See examples/greeter.toml for every key (appearance.palette, output, …).
  settings = {
    cursor = {
      theme = "Bibata-Modern-Ice";
      size = 24;
      path = "${pkgs.bibata-cursors}/share/icons";
    };
    keyboard = {
      layout = "us";
    };
  };
};
```

The module enables greetd and sets the session command automatically. It also enables
`accounts-daemon` (user avatars), Polkit, and the `pkexec` wrapper where that
option exists, keeping administrator-prompted sync available by default.

`passwordless-sync-users` is optional. It requires Noctalia Greeter 1.4.0 or
newer together with the next Noctalia release after 5.0.1. Current `-git`
packages, `main` checkouts, or manual builds from current `main` work when both
projects are up to date. Leave the option unset or empty to require
administrator authentication for every sync. Older and mixed-version
combinations continue to use the administrator-authenticated legacy sync path.

`greeter-args` passes extra flags to `noctalia-greeter-session`, for example
`--session <name>` to set a default session. Run `noctalia-greeter sessions`
to list valid names.

`settings` writes `/var/lib/noctalia-greeter/greeter.toml` (full declarative config,
including appearance/palette when you set them). Sync/UI mutable data lives in
`sync.toml` (not managed by Nix). Full docs:
**[docs.noctalia.dev/greeter](https://docs.noctalia.dev/greeter/)**.
Commented example: [`examples/greeter.toml`](examples/greeter.toml).

### Build from source

Requires [just](https://github.com/casey/just) and [meson](https://mesonbuild.com/).

#### Release build

For a system-wide manual install with managed passwordless appearance sync,
use the root-owned `/usr` prefix so the Polkit action lands in the standard
action directory:

```sh
meson setup build-release --prefix=/usr --buildtype=release
just build-release
sudo meson install -C build-release
sudo ./scripts/setup_greeter_system.sh
```

To reconfigure an existing build directory:

```sh
meson setup build-release --prefix=/usr --buildtype=release --reconfigure
```

Meson's default `/usr/local` prefix is usable for the login greeter and the
administrator-authenticated fallback, but Polkit normally loads action
definitions from `/usr/share/polkit-1/actions`. Use `--prefix=/usr` for the
dedicated passwordless action unless your distribution explicitly configures
another action directory.

#### Debug build

```sh
just build
sudo just install
```

`just install` runs the same system setup scripts after Meson install.

Meson installs the greeter binary, session launcher and assets. With the default prefix that is under `/usr/local`; distro packages and the passwordless-ready manual command above install to `/usr/bin` instead.

```text
<prefix>/bin/noctalia-greeter
<prefix>/bin/noctalia-greeter-compositor
<prefix>/bin/noctalia-greeter-session
<prefix>/bin/noctalia-greeter-apply-appearance
<prefix>/share/noctalia-greeter/assets/...
<prefix>/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy
```

The greeter needs the shipped `assets/` tree at runtime. Copying only the `noctalia-greeter` binary is not enough.

## Setting up greetd

Point greetd at the installed session wrapper. Use the path on your system - do not assume `/usr/local` if you installed from a package:

```sh
which noctalia-greeter-session
```

Example for a manual install to `/usr/local` (replace the path if `which` shows something else, e.g. `/usr/bin/noctalia-greeter-session`):

```toml
[default_session]
command = "/usr/local/bin/noctalia-greeter-session"
user = "greeter"
```

Use the `user` value that matches your greetd config. `setup_greeter_system.sh` prints a ready-to-paste `config.toml` block with the path it finds. It also prepares `/var/lib/noctalia-greeter/` for that account.

Optional default session (must match a name from the session picker, e.g. `niri`):

```toml
command = "/usr/bin/noctalia-greeter-session -- --session niri"
```

List valid session names:

```sh
noctalia-greeter sessions
```

Sessions come from `wayland-sessions` `.desktop` files under `/usr/share`, each path in `XDG_DATA_DIRS`, and on NixOS `/run/current-system/sw/share`. On login the greeter passes `XDG_SESSION_TYPE=wayland` plus `XDG_CURRENT_DESKTOP` / `XDG_SESSION_DESKTOP` from `DesktopNames=` through greetd (before PAM), so logind sees a Wayland session. GNOME remains best-effort versus GDM.

### Multi-monitor

By default the greeter is **mirrored on every connected monitor** (same UI on each display, each sized to that output's own resolution and scale). To pin it to a single connector, set `[output].name` in `/var/lib/noctalia-greeter/greeter.toml`:

```toml
[output]
name = "DP-2"
```

The compositor disables the other connectors at the KMS level when `[output].name` is set. If it names a disconnected connector, the greeter falls back to mirroring on all outputs.

When using multiple monitors, set `[output].layout` manually or sync from Noctalia Shell (see Matching Noctalia Shell). Without it, outputs are placed left-to-right by connector name. Portrait panels need `[output].transforms` (also synced from the shell) so the greeter UI is upright. List connector names with `noctalia-greeter outputs`:

```toml
[output]
layout = "DP-1:0,0; HDMI-A-1:1920,0; DP-2:3840,0"
transforms = "DP-1:normal; HDMI-A-1:270"
```

Coordinates are logical pixels from your desktop compositor. The greeter uses
them as absolute positions; pair them with matching `[output].scales` values so
the configured edges remain adjacent and the cursor can move between monitors.

Test locally with:

```sh
just run
```

On high-DPI panels (for example 4K), the greeter compositor applies fractional output scaling from the monitor's physical size when EDID reports it, otherwise from resolution. Scale is capped at 2×. The greeter client lays out at logical size and renders HiDPI buffers via Wayland fractional scale.

To override auto scaling, set `[output].scale` in `greeter.toml` (read by the compositor):

```toml
[output]
scale = 1.5
```

If `[output].scale` is missing or invalid, the compositor next checks
per-output scales from `greeter.toml` or `sync.toml`. With an explicit layout,
an output without a matching scale uses `1.0`; otherwise scaling falls back to
the display's physical geometry and resolution.

### Idle blanking

By default the greeter never blanks the screen. Set `[idle].timeout` (seconds, `0`-`86400`) to turn off DRM outputs after idle input; `0` or omitting the key disables blanking. `NOCTALIA_GREETER_IDLE_TIMEOUT` on the greetd session command overrides the file when set.

```toml
[idle]
timeout = 300
```

On NixOS: `programs.noctalia-greeter.settings.idle.timeout = 300;` (written to `greeter.toml`).

List connector names from a running Wayland session:

```sh
noctalia-greeter outputs
```

Restart greetd:

```sh
sudo systemctl restart greetd
```

On runit:

```sh
sudo sv restart greetd
```

Create state/log paths once if needed. Prefer the portable setup (works without systemd):

```sh
sudo ./scripts/setup_greeter_system.sh
# or during development:
just setup-log-dir
```

On systemd (or opentmpfiles), installs also ship `/usr/lib/tmpfiles.d/noctalia-greeter.conf` so the state dir can be recreated with `systemd-tmpfiles --create`. That drop-in hardcodes the `greeter` user; use the setup script when your greetd user differs.

Logging defaults to **syslog** under greetd (journald on systemd, metalog/syslog-ng/etc. on OpenRC). The session wrapper parks stdout/stderr so wlroots/libseat chatter does not flash the VT. Override with `NOCTALIA_GREETER_LOG=stderr` for console debugging (info → stdout, debug/warn/error → stderr), or `NOCTALIA_GREETER_LOG=/path` for a log file:

```toml
command = "env NOCTALIA_GREETER_LOG=stderr WLR_LOG=info /usr/bin/noctalia-greeter-session"
```

```toml
command = "env NOCTALIA_GREETER_LOG=/var/lib/noctalia-greeter/greeter.log /usr/bin/noctalia-greeter-session"
```

On systemd, inspect greeter lines with `journalctl -u greetd` (or your greetd unit name) and look for the `noctalia-greeter` syslog identifier.

## Matching Noctalia Shell

With [Noctalia v5](https://github.com/noctalia-dev/noctalia) installed, open
**Settings → Security → Noctalia Greeter → Sync Now**. Noctalia Greeter 1.4.0
or newer together with the next Noctalia release after 5.0.1 uses the
constrained `pkexec noctalia-greeter-apply-appearance --sync <staging-dir>`
path. Current `-git` packages, `main` checkouts, or manual builds from current
`main` also support it when both projects are up to date and the manual helper
is installed into a root-owned, non-user-writable system prefix. This path
accepts appearance, wallpaper, and display data, but not session power commands
or custom actions.

Passwordless authorization is optional. Without a site-local Polkit allow rule,
the constrained path asks for administrator authentication every time. Older
or mixed-version combinations continue to use the permanently supported legacy
positional helper call, authenticated through `run0`, `pkexec`, or Noctalia's
configured privilege-command prefix.

On conventional packaged distributions, enable the narrow passwordless rule
for a local login account with:

```sh
sudo noctalia-greeter passwordless-sync enable alice
```

Inspect it with `noctalia-greeter passwordless-sync status [USER]` (add `sudo`
if your Polkit rules directory is not readable), and remove that account from
the CLI-managed authorization with
`sudo noctalia-greeter passwordless-sync disable alice`. Prompts resume when no
other administrator-authored allow rule matches. NixOS users configure the same
access declaratively with `passwordless-sync-users` in the project module, or
an equivalent `security.polkit.extraConfig` rule.

This setup affects only the constrained appearance-only action. It does not
make the legacy helper passwordless and does not enable Noctalia's Auto-Sync
setting.

Both paths merge their data into live `sync.toml`; declarative `greeter.toml`
is never overwritten, and keys set there win over Sync.

Authorization, passwordless setup, runtime constraints, and troubleshooting:
**[Sync with Noctalia](https://docs.noctalia.dev/greeter/sync/)**.

## Cursor theme

The compositor resolves the cursor theme, size and search path in this order:

1. `[cursor].theme` / `[cursor].size` / `[cursor].path` in `greeter.toml` (above)
2. The `XCURSOR_THEME`, `XCURSOR_SIZE` and `XCURSOR_PATH` environment variables
3. The wlroots defaults (built-in cursor at size `24`)

greetd starts greeters with an empty environment, so to use the environment
variables set them in the greetd session **command** rather than the service
environment, for example in `/etc/greetd/config.toml`:

```toml
[default_session]
command = "env XCURSOR_THEME=Adwaita XCURSOR_SIZE=24 /usr/bin/noctalia-greeter-session"
```

If the theme is not under the default search path, also set
`XCURSOR_PATH` (or `cursor.path`) to the directory that contains it.

On NixOS, set `programs.noctalia-greeter.settings.cursor` (writes `greeter.toml`);
use `path = "${pkgs.<theme>}/share/icons"` for packaged themes. The module does not
add a `package` option or wrap the greetd command with `XCURSOR_*`.

## Keyboard

The greeter works without a mouse.

### Layout

The compositor builds an XKB keymap in this precedence order:

1. `greeter.toml`: `[keyboard].layout` / `.variant` / `.options`
2. `XKB_DEFAULT_LAYOUT` / `XKB_DEFAULT_VARIANT` / `XKB_DEFAULT_OPTIONS` (environment)
3. The system default keymap

Example:

```toml
[keyboard]
layout = "cz"
```

Multiple layouts:

```toml
[keyboard]
layout = "us,cz"
options = "grp:alt_shift_toggle"
```

Use standard [XKB layout codes](https://www.freedesktop.org/wiki/Software/XKeyboard-config/Rules/) (`de`, `fr`, `ru`, ...).

greetd starts greeters with an empty environment, so set layout in `greeter.toml` or prefix the greetd session command:

```toml
[default_session]
command = "env XKB_DEFAULT_LAYOUT=cz /usr/bin/noctalia-greeter-session"
```

### Shortcuts

| Key | Action |
|-----|--------|
| `Tab` / `Shift+Tab` | Move focus |
| `↑` / `↓` | Move focus, or move in an open menu |
| `Enter` | Submit password / activate / confirm menu |
| `Space` | Activate focused control |
| `Esc` | Close menu or leave password step |
| `F3` | Session picker |
| `F7` | Color scheme picker |
| `Ctrl+Alt+F1`-`F12` | Switch to virtual terminal (TTY) |

## Troubleshooting

- **Blank screen** - Check greetd logs: `journalctl -u greetd` (syslog identifier `noctalia-greeter`) or your system logger. If you set `NOCTALIA_GREETER_LOG` to a path or `stderr`, check that sink instead. Ensure `/var/lib/noctalia-greeter` exists (`just setup-log-dir` / `setup_greeter_system.sh`).
- **Wrong greeter size / only one monitor looks right** - Confirm `[output].name` in `greeter.toml` matches a connector from `noctalia-greeter outputs`. Restart greetd after changing it.
- **Black screen after reboot** - Same as blank screen: greetd/syslog first. Confirm the state dir and synced appearance files are present.
- **`Failed to spawn client` / wrong path in greetd config** - `command` must be the full path from `which noctalia-greeter-session` (often `/usr/bin/...` on packaged installs, not `/usr/local/bin/...`).
- **`WAYLAND_DISPLAY is not set`** - greetd must use `noctalia-greeter-session` (it starts `noctalia-greeter-compositor`). Fix `command` in `/etc/greetd/config.toml`.
- **`Login service stopped responding`** - greetd did not reply within the configured timeout. Inspect the greetd journal for a stalled or crashed PAM/session worker, then restart greetd. `[auth].request_timeout` controls the watchdog (default `60` seconds; `0` disables it).
- **Wrong session on startup** - If `[session].default` is set in `greeter.toml`, it wins over last-used `[session].last`. Run `noctalia-greeter sessions` for exact **Name** spelling.
- **Synced look missing** - Install shell v5 and greeter; run **Sync Now** in shell settings again; restart greetd or log out once.

Stuck display over SSH:

```sh
just recover
```

---

## Scope

Noctalia Greeter is a **display/login greeter** for greetd. It handles user/session selection and authentication UI.

It is **not** a desktop shell or compositor replacement.

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contributor guidance.

---

## License

Distributed under the MIT License. See [LICENSE](LICENSE) for details.
