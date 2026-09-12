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

### Arch Linux

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

### openSUSE Tumbleweed and Slowroll

```sh
sudo zypper install meson gcc-c++ just \
  greetd dbus-1 \
  wayland-devel wayland-protocols wlroots-devel \
  Mesa-libEGL-devel Mesa-libGLESv2-devel \
  freetype2-devel fontconfig-devel \
  cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel \
  tomlplusplus-devel nlohmann_json-devel stb-devel \
  libwebp-devel librsvg-devel libxml2-devel
```

### Debian and Ubuntu

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

## Build and install

From a cloned Noctalia Greeter checkout, build and install a release under the
system prefix:

```sh
meson setup build-release --prefix=/usr --buildtype=release
just build-release
sudo meson install -C build-release
sudo ./scripts/setup_greeter_system.sh
```

The setup script prepares `/var/lib/noctalia-greeter/` and `greeter.toml` for
the greetd user.

Continue with [Configure greetd](installation.md#3-configure-greetd) to merge
the installed session wrapper into your greetd configuration and complete the
safe display-manager cutover.
