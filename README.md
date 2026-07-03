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
  libwebp librsvg
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
  libwebp-devel librsvg2-devel
```

### openSUSE (Tumbleweed & Slowroll)

```sh
sudo zypper install meson gcc-c++ just \
  greetd dbus-1 \
  wayland-devel wayland-protocols-devel wlroots-devel \
  Mesa-libEGL-devel Mesa-libGLESv2-devel \
  freetype2-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  libwebp-devel librsvg-devel
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
  libwebp-dev librsvg2-dev
```

### Void Linux

```sh
sudo xbps-install meson ninja pkg-config git \
  greetd dbus \
  wayland-devel wayland-protocols wlroots-devel libepoxy-devel \
  MesaLib-devel libglvnd-devel cairo-devel \
  pango-devel fontconfig-devel freetype-devel harfbuzz-devel \
  libxkbcommon-devel libwebp-devel librsvg-devel
```

Vendored dependencies, with no system package needed: `nlohmann/json`, `stb`, and `Wuffs`.

Dependencies that are vendored by default, with a meson option to
instead use the system package: `tomlplusplus`

Build requires `wlroots-0.20` and `wayland-server` development packages (see distro lists above).

`libwebp` handles WebP wallpapers when syncing appearance from the shell. Wuffs handles other raster image formats.

On Void Linux, `libepoxy-devel` is used when EGL/GLES pkg-config modules are not available.

## NixOS

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
  package = inputs.noctalia-greeter.packages.${pkgs.stdenv.hostPlatform.system}.default;

  # Optional configuration
  greeter-args = "";
  settings = {
    cursor = {
      theme = "Adwaita";
      size = 24;
    };
    keyboard = {
      layout = "us";
    };
  };
};
```

The module enables greetd and sets the session command automatically.

`greeter-args` passes extra flags to `noctalia-greeter-session`, for example
`--session <name>` to set a default session. Run `noctalia-greeter sessions`
to list valid names.

`settings` accepts a Nix attrset, a raw TOML string, or a path to a `.toml` file, 
and is copied to `/var/lib/noctalia-greeter/greeter.toml` on boot.

## Building and installing

Requires [just](https://github.com/casey/just) and [meson](https://mesonbuild.com/).

#### Release build

```sh
just configure-release
just build-release
sudo meson install -C build-release
sudo ./scripts/setup_greeter_system.sh
```

Pass a prefix when configuring to install somewhere other than `/usr/local`:

```sh
meson setup build-release --prefix="$HOME/.local" --buildtype=release --reconfigure
just build-release
sudo meson install -C build-release
sudo ./scripts/setup_greeter_system.sh
```

#### Debug build

```sh
just build
sudo just install
```

`just install` runs the same system setup scripts after Meson install.

Meson installs the greeter binary, session launcher and assets. With the default prefix that is under `/usr/local`; distro packages (e.g. AUR) usually install to `/usr/bin` instead.

```text
<prefix>/bin/noctalia-greeter
<prefix>/bin/noctalia-greeter-compositor
<prefix>/bin/noctalia-greeter-session
<prefix>/bin/noctalia-greeter-apply-appearance
<prefix>/share/noctalia-greeter/assets/...
```

The greeter needs the shipped `assets/` tree at runtime. Copying only the `noctalia-greeter` binary is not enough.

## Setting up greetd

Point greetd at the installed session wrapper. Use the path on your system — do not assume `/usr/local` if you installed from a package:

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

Sessions come from `wayland-sessions` `.desktop` files under `/usr/share`, each path in `XDG_DATA_DIRS`, and on NixOS `/run/current-system/sw/share`.

### Multi-monitor

By default the greeter is **mirrored on every connected monitor** (same UI on each display, sized to the primary output). To pin it to a single connector, set `[output].name` in `/var/lib/noctalia-greeter/greeter.toml`:

```toml
[output]
name = "DP-2"
```

The compositor disables the other connectors at the KMS level when `[output].name` is set. If it names a disconnected connector, the greeter falls back to mirroring on all outputs.

When using multiple monitors, set `[output].layout` manually or sync from Noctalia Shell (see Matching Noctalia Shell). Without it, outputs are placed left-to-right by connector name. List connector names with `noctalia-greeter outputs`:

```toml
[output]
layout = "DP-1:0,0; HDMI-A-1:1920,0; DP-2:3840,0"
```

Coordinates are logical pixels from your desktop compositor. The greeter uses them for **order and row grouping**, then places outputs edge-to-edge at its own scale so the cursor can move between monitors.

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

If `[output].scale` is missing or invalid, the compositor falls back to auto scaling.

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

Create log files once if needed:

```sh
just setup-log-dir
```

Logs: `/var/log/noctalia-greeter.log` and `/var/lib/noctalia-greeter/greeter.log`.

## Matching Noctalia Shell

With [Noctalia v5](https://github.com/noctalia-dev/noctalia) installed, open **Settings → Shell → Security → Noctalia Greeter → Sync Now**. The shell copies your wallpaper, palette, and multi-monitor layout (when available) to the greeter (you may be prompted for admin credentials). After syncing, log out or restart greetd to see the changes on the login screen.

The greeter adds a **Synced** color scheme when sync data is present. Session and scheme choices you make on the login screen are remembered in `/var/lib/noctalia-greeter/greeter.toml`.

Admin-only settings in `greeter.toml` (set by you, not the UI):

- `[session].default` - session selected when the greeter opens (overrides last-used unless you pass `--session` on the command line)
- `[user].default` - username to select on startup; opens the password step immediately (`--user` on the command line wins)
- `[output].name` - Wayland connector name (see Multi-monitor)
- `[output].layout` - multi-monitor positions as `NAME:X,Y; ...` in logical pixels (see Multi-monitor)
- `[output].scale` - manual compositor scale factor (e.g. `1.5`); invalid or missing → auto scale
- `[cursor].theme` - cursor theme name (e.g. `Adwaita`); missing → wlroots default cursor
- `[cursor].size` - cursor size in pixels (e.g. `24`); missing → `24`
- `[cursor].path` - colon-separated theme search path (sets `XCURSOR_PATH`)
- `[keyboard].layout` / `.variant` / `.options` - XKB keymap (compositor)
- `[appearance].password_style` - password mask style: `default` (filled circles) or `random` (cycled glyph shapes, same as Noctalia shell)

The greeter updates `[session].last` and `[appearance].scheme` when you change them in the UI.

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

Use standard [XKB layout codes](https://www.freedesktop.org/wiki/Software/XKeyboard-config/Rules/) (`de`, `fr`, `ru`, …).

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
| `Ctrl+Alt+F1`–`F12` | Switch to virtual terminal (TTY) |

## Troubleshooting

- **Blank screen** - Check `/var/log/noctalia-greeter.log` and `/var/lib/noctalia-greeter/greeter.log`. Run `just setup-log-dir` if they are missing.
- **`Failed to spawn client` / wrong path in greetd config** - `command` must be the full path from `which noctalia-greeter-session` (often `/usr/bin/...` on packaged installs, not `/usr/local/bin/...`).
- **`WAYLAND_DISPLAY is not set`** - greetd must use `noctalia-greeter-session` (it starts `noctalia-greeter-compositor`). Fix `command` in `/etc/greetd/config.toml`.
- **Black screen after reboot** - logs survive under `/var/log/noctalia-greeter.log` and `/var/lib/noctalia-greeter/greeter.log` (run `just setup-log-dir` once). Session output also goes there when writable.
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

Contributions are welcome: fixes, polish, docs, or UX improvements.

- Open an issue for bugs and regressions
- Open a PR for improvements

---

## License

Distributed under the MIT License. See [LICENSE](LICENSE) for details.
