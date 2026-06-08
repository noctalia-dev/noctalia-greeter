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

Pair it with **[Noctalia Shell v5](https://github.com/noctalia-dev/noctalia-shell/tree/v5)** if you want wallpaper and palette synced from the shell settings (optional).

## Dependencies

Install everything below on the machine where greetd will run. Each list covers build tools and libraries, plus **greetd**, **Cage**, and **D-Bus** (used by `noctalia-greeter-session`). You still need your desktop sessions separately (niri, Hyprland, and so on).

### Arch

```sh
sudo pacman -S meson gcc just \
  greetd cage dbus \
  wayland wayland-protocols \
  libglvnd freetype2 fontconfig \
  cairo pango \
  libxkbcommon glib2 \
  libwebp librsvg
```

### Fedora

```sh
sudo dnf install meson gcc-c++ just \
  greetd cage dbus \
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
  greetd cage dbus \
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
  greetd cage dbus \
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

Meson installs the greeter binary, session launcher, assets, and a PolicyKit policy file (consumed when the shell syncs appearance):

```text
/usr/local/bin/noctalia-greeter
/usr/local/bin/noctalia-greeter-session
/usr/local/bin/noctalia-greeter-apply-appearance
/usr/local/share/noctalia-greeter/assets/...
```

The greeter needs the shipped `assets/` tree at runtime. Copying only the `noctalia-greeter` binary is not enough.

## Setting up greetd

`just install` and `setup_greeter_system.sh` prepare `/var/lib/noctalia-greeter/`, logs, and `greeter.conf`. They do **not** edit `/etc/greetd/config.toml` for you.

After install, print a copy-paste block with paths and greeter user resolved for your machine:

```sh
just print-greetd-config
# or, when installed:
noctalia-greeter-print-greetd-config
```

Copy the printed commands into a terminal. On an existing greetd setup the block backs up `config.toml` to `config.toml.bak` before replacing it.

Manual edit instead (path matches your prefix):

```toml
[default_session]
command = "/usr/local/bin/noctalia-greeter-session"
user = "greeter"
```

Optional pinned desktop session (name must match the picker, e.g. `niri`):

```toml
command = "/usr/local/bin/noctalia-greeter-session -- --session niri"
```

List valid session names:

```sh
noctalia-greeter sessions
```

Logs: `/var/log/noctalia-greeter.log` and `/var/lib/noctalia-greeter/greeter.log`. Run `just setup-log-dir` if they are missing.

## Matching Noctalia Shell

With [Noctalia Shell v5](https://github.com/noctalia-dev/noctalia-shell/tree/v5) installed on your desktop (polkit required there, not on the greetd host), open **Settings → Shell → Security → Noctalia Greeter → Sync Now**. The shell stages files and runs `pkexec noctalia-greeter-apply-appearance` to install them under `/var/lib/noctalia-greeter/`. After syncing, log out or restart greetd to see the changes on the login screen.

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
- **`WAYLAND_DISPLAY is not set`** - greetd must use `noctalia-greeter-session` (it starts Cage). Fix `command` in `/etc/greetd/config.toml`.
- **Wrong session on startup** - If `default_session` is set in `greeter.conf`, it wins over last-used `session`. Run `noctalia-greeter sessions` for exact **Name** spelling.
- **Synced look missing** - Install the greeter on the greetd host and shell v5 (with polkit) on your desktop session; use **Sync Now** again; restart greetd or log out once.

Stuck display over SSH:

```sh
just recover
```

## Contributing

We welcome contributions of any size - bug fixes, new features, documentation improvements!

## License

Distributed under the MIT License. See [LICENSE](LICENSE) for details.
