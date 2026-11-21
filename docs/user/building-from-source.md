---
title: Building from source
description: Build and install Noctalia Greeter on distributions without a package.
sidebar:
  order: 1.5
---

Use this route when your distribution does not provide a Noctalia Greeter
package. Install the build dependencies for your distribution, then build from
a cloned Noctalia Greeter checkout.

## Build dependencies

Use Meson >= 1.1 and a compiler/standard library with C++23 support (including
`std::expected` and `std::format`). The compositor still builds as C against
wlroots 0.20. The shared Noctalia UI library is included in `lib/`; no separate
shell checkout, submodule download, or network fetch is needed to build it.


### Arch Linux

```sh
sudo pacman -S --needed base-devel meson just \
  greetd dbus \
  wayland wayland-protocols wlroots0.20 libinput \
  libglvnd freetype2 fontconfig \
  cairo pango harfbuzz \
  libxkbcommon glib2 \
  tomlplusplus nlohmann-json stb \
  libwebp libjxl librsvg libxml2
```

### Fedora

```sh
sudo dnf install meson gcc-c++ just \
  greetd dbus \
  wayland-devel wayland-protocols-devel wlroots-devel libinput-devel \
  libEGL-devel mesa-libGLES-devel \
  freetype-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  tomlplusplus-devel json-devel stb_image_resize2-devel \
  libwebp-devel libjxl-devel librsvg2-devel libxml2-devel
```

### openSUSE Tumbleweed and Slowroll

```sh
sudo zypper install meson gcc-c++ just \
  greetd dbus-1 \
  wayland-devel wayland-protocols wlroots-devel libinput-devel \
  Mesa-libEGL-devel Mesa-libGLESv2-devel \
  freetype2-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  tomlplusplus-devel nlohmann_json-devel stb-devel \
  libwebp-devel libjxl-devel librsvg-devel libxml2-devel
```

### Debian and Ubuntu

```sh
sudo apt install meson g++ just \
  greetd dbus \
  libwayland-dev wayland-protocols libwlroots-0.20-dev libinput-dev \
  libegl-dev libgles-dev \
  libfreetype-dev libfontconfig-dev \
  libcairo2-dev libpango1.0-dev libharfbuzz-dev \
  libxkbcommon-dev libglib2.0-dev \
  libtomlplusplus-dev nlohmann-json3-dev libstb-dev \
  libwebp-dev libjxl-dev librsvg2-dev libxml2-dev
```

### Void Linux

```sh
sudo xbps-install meson ninja pkg-config git \
  greetd dbus \
  wayland-devel wayland-protocols wlroots-devel libinput-devel libepoxy-devel \
  MesaLib-devel libglvnd-devel cairo-devel \
  pango-devel fontconfig-devel freetype-devel harfbuzz-devel \
  tomlplusplus-devel nlohmann-json-devel stb \
  libxkbcommon-devel libwebp-devel libjxl-devel librsvg-devel libxml2-devel
```

## Build and install

From a cloned Noctalia Greeter checkout, build and install a release under the
system prefix:

```sh
meson setup build-release --prefix=/usr --buildtype=release
just build-release
sudo meson install -C build-release
```

The `just` configure recipes explicitly select C++23, including for existing build directories that cached C++20. For a manually managed build directory, run `meson configure build -Dcpp_std=c++23` before rebuilding.

After configuring greetd's session user, initialize greeter state with the
installed helper:

```sh
sudo noctalia-greeter-apply-appearance --setup-system
```

See [state initialization](installation.md#initialize-greeter-state) for
directory ownership and tmpfiles overrides for custom greeter accounts.

The portable setup script also prepares
`/var/lib/noctalia-greeter/`, `greeter.toml`, and `sync.toml` for the greetd user
and adjusts greetd's PAM stack for the detected systemd or elogind runtime
module. Run it when that PAM adjustment is appropriate for your distribution:

```sh
sudo ./scripts/setup_greeter_system.sh
```

The state helper above also works on systems without tmpfiles processing,
where it creates the directory as well. Set `GREETER_USER` explicitly if its
existing owner differs from the account you intend to use.

Continue with [Configure greetd](installation.md#3-configure-greetd) to merge
the installed session wrapper into your greetd configuration and complete the
safe display-manager cutover.

## Preview locally

From a terminal in an existing Wayland session, build and run a nested greeter:

```sh
WLR_BACKENDS=wayland just run-local
```

This uses the bundled compositor inside the current session. Logs go to `~/.cache/noctalia-greeter.log` unless `NOCTALIA_GREETER_LOG` overrides the path. Stop the preview with Ctrl+C in its terminal. Authentication requires a greetd test socket.

Use `just run-niri` to run the client directly in the current Wayland session. It works with other Wayland compositors too. The normal greeter deployment uses `noctalia-greeter-session` as described in the install instructions.

The bundled compositor accepts login windows only from the greeter process it
launches. A custom `NOCTALIA_GREETER_BIN`/`GREETER_BIN` script must end with
`exec /path/to/noctalia-greeter "$@"` so the executable keeps that process ID.
Use `just run-local` for nesting: the installed session wrapper clears inherited
`WAYLAND_DISPLAY` when preparing a greetd session.

## Verify a build

```sh
meson test -C build --print-errorlogs
meson setup build-asan --buildtype=debug -Db_sanitize=address -Db_lundef=false -Dcpp_std=c++23
meson test -C build-asan --print-errorlogs
```

The graphical tests create private headless compositor sessions and fake greetd sockets. They cover login handoff, cancellation and account isolation, menu focus, resizing, and idle rendering. The suite also checks layout/configuration fallback, output handling, wallpaper rendering, sessions, and version metadata. Headless tests require permission to create Unix sockets and initialize the graphics stack.

The session-wrapper test uses a private user/mount namespace with a temporary
`/tmp` to check runtime-directory ownership without touching a live session.
It reports a skip when those namespace operations are unavailable. The setup
config-preservation test similarly requires an unprivileged user namespace.

Before deployment, verify actual PAM authentication, monitor/input hardware, output hotplug, and suspend/resume on the target system. Use a temporary `DESTDIR` when checking package contents; the application artwork and `assets/library/` font files are required at runtime.
