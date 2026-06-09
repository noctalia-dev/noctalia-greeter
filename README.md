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

It is built for **greetd**: greetd starts a small Wayland compositor ([Cage](https://github.com/cage-kiosk/cage)), and the greeter runs inside that session. It is a login UI only, not a desktop shell or compositor.

Pair it with **[Noctalia v5](https://github.com/noctalia-dev/noctalia)** if you want wallpaper and palette synced from the shell settings (optional).

## Dependencies

Install everything below on the machine where greetd will run. Each list covers build tools and libraries, plus **greetd**, **Cage**, and **D-Bus** (used by `noctalia-greeter-session`). You still need your desktop sessions separately (niri, Hyprland, and so on).

### Arch

```sh
sudo pacman -S meson gcc just \
  greetd cage wlr-randr dbus polkit \
  wayland wayland-protocols \
  libglvnd freetype2 fontconfig \
  cairo pango \
  libxkbcommon glib2 \
  libwebp librsvg
```

### Fedora

```sh
sudo dnf install meson gcc-c++ just \
  greetd cage wlr-randr dbus polkit \
  wayland-devel wayland-protocols-devel \
  libEGL-devel mesa-libGLES-devel \
  freetype-devel fontconfig-devel \
  cairo-devel pango-devel \
  libxkbcommon-devel glib2-devel \
  libwebp-devel librsvg2-devel
```

### Debian / Ubuntu

```sh
sudo apt install meson g++ just \
  greetd cage wlr-randr dbus policykit-1 \
  libwayland-dev wayland-protocols \
  libegl-dev libgles-dev \
  libfreetype-dev libfontconfig-dev \
  libcairo2-dev libpango1.0-dev \
  libxkbcommon-dev libglib2.0-dev \
  libwebp-dev librsvg2-dev
```

### Void Linux

```sh
sudo xbps-install meson ninja pkg-config git \
  greetd cage wlr-randr dbus polkit \
  wayland-devel wayland-protocols libepoxy-devel \
  MesaLib-devel libglvnd-devel cairo-devel \
  pango-devel fontconfig-devel freetype-devel \
  libxkbcommon-devel libwebp-devel librsvg-devel
```

Vendored dependencies, with no system package needed: `nlohmann/json`, `stb`, and `Wuffs`.

`libwebp` handles WebP wallpapers when syncing appearance from the shell. Wuffs handles other raster image formats.

On Void Linux, `libepoxy-devel` is used when EGL/GLES pkg-config modules are not available.

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

The greeter runs inside Cage, which can span all connected outputs. By default it shows on the **primary monitor only** (largest by pixel area). After Wayland connect it uses `wlr-randr` to turn off the other connectors so the login UI does not stretch across every display.

To pin the greeter to a specific connector, set `output` in `/var/lib/noctalia-greeter/greeter.conf`:

```ini
output="DP-2"
```

`wlr-randr` is required (see Dependencies). If `output` is missing, empty, or names a disconnected connector, the greeter falls back to the primary display only.

When `wlr-randr` cannot disable every other connector (some setups keep the primary on), the greeter letterboxes onto the chosen or primary output inside Cage's combined desktop instead of stretching the UI.

On high-DPI panels (for example 4K without fractional scaling), the greeter scales its UI from the monitor's physical size when EDID reports it, otherwise from resolution. Scale is capped at 2×.

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

With [Noctalia Shell v5](https://github.com/noctalia-dev/noctalia-shell/tree/v5) installed, open **Settings → Shell → Security → Noctalia Greeter → Sync Now**. The shell copies your wallpaper and palette to the greeter (admin prompt via polkit). After syncing, log out or restart greetd to see the changes on the login screen.

The greeter adds a **Synced** color scheme when sync data is present. Session and scheme choices you make on the login screen are remembered in `/var/lib/noctalia-greeter/greeter.conf`.

Admin-only keys in `greeter.conf` (set by you, not the UI):

- `default_session` - session selected when the greeter opens (overrides last-used unless you pass `--session` on the command line)
- `greeter_user` - greetd account name (setup/logging)

The greeter updates `session` and `scheme` when you change them in the UI.

## Keyboard

The greeter works without a mouse.

| Key | Action |
|-----|--------|
| `Tab` / `Shift+Tab` | Move focus |
| `↑` / `↓` | Move focus, or move in an open menu |
| `Enter` | Submit password / activate / confirm menu |
| `Space` | Activate focused control |
| `Esc` | Close menu or leave password step |
| `F3` | Session picker |
| `F7` | Color scheme picker |

## Troubleshooting

- **Blank screen** - Check `/var/log/noctalia-greeter.log` and `/var/lib/noctalia-greeter/greeter.log`. Run `just setup-log-dir` if they are missing.
- **`Failed to spawn client` / wrong path in greetd config** - `command` must be the full path from `which noctalia-greeter-session` (often `/usr/bin/...` on packaged installs, not `/usr/local/bin/...`).
- **`WAYLAND_DISPLAY is not set`** - greetd must use `noctalia-greeter-session` (it starts Cage). Fix `command` in `/etc/greetd/config.toml`.
- **Wrong session on startup** - If `default_session` is set in `greeter.conf`, it wins over last-used `session`. Run `noctalia-greeter sessions` for exact **Name** spelling.
- **Synced look missing** - Install shell v5, greeter, and the polkit policy; sync again; restart greetd or log out once.

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
