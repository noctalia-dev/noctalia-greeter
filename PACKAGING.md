# Packaging Noctalia Greeter

Notes for distribution packagers. End-user install docs live in the
[README](README.md) and at [docs.noctalia.dev](https://docs.noctalia.dev).

## Package description

Use this short description for package metadata (`pkgdesc`, `Summary`,
AppStream, etc.):

> A minimal login greeter for greetd that matches the look and feel of Noctalia Shell.

## Identity

| | |
|---|---|
| Name | `noctalia-greeter` |
| Homepage | https://github.com/noctalia-dev/noctalia-greeter |
| Docs | https://docs.noctalia.dev |
| License | MIT ([LICENSE](LICENSE)) |
| Version | Meson `project(... version: ...)` in [`meson.build`](meson.build) |
| Session wrapper | `noctalia-greeter-session` (what greetd should run) |

## Build

- Build system: [Meson](https://mesonbuild.com/) + Ninja. The repo [`Justfile`](Justfile) is convenience only.
- Language: C++20 (greeter client), C (wlroots compositor).
- Recommended for packages: `meson setup build --buildtype=plain`.
- **wlroots 0.20** and **wayland-server** are required (the compositor is not optional).

Install with the prefix you intend to ship. Greetd `command` must point at the installed `noctalia-greeter-session` path (often `/usr/bin/...`, not `/usr/local/bin/...`).

### Installed layout

```text
<prefix>/bin/noctalia-greeter
<prefix>/bin/noctalia-greeter-compositor
<prefix>/bin/noctalia-greeter-session
<prefix>/bin/noctalia-greeter-apply-appearance
<prefix>/bin/noctalia-greeter-print-greetd-config   # optional helper
<prefix>/share/noctalia-greeter/assets/...
<prefix>/share/noctalia-greeter/setup_greeter_system.sh
<prefix>/share/polkit-1/actions/org.noctalia.greeter.apply-appearance.policy   # optional; see Polkit
<prefix>/lib/tmpfiles.d/noctalia-greeter.conf       # optional; see below
```

The shipped `assets/` tree is **required at runtime**. Copying only `noctalia-greeter` breaks fonts, icons, and UI resources.

Runtime override: `NOCTALIA_GREETER_ASSETS_DIR` points at an alternate assets tree.

## Dependencies

No Qt or GTK. UI is Wayland + OpenGL ES (EGL/GLES, or `libepoxy` when separate
EGL/GLES pkg-config modules are missing).

### Build-time (required)

Canonical list is the `dependency(...)` / header checks in
[`meson.build`](meson.build). The [README](README.md) has copy-paste install
lines for common distros (Arch, Fedora, openSUSE, Debian, Void).

Notes packagers hit often:

- **wlroots 0.20** (`wlroots-0.20` pkg-config) and **wayland-server** are
  required. The compositor is not optional.
- **stb** must provide `stb/stb_image_resize2.h` (older `stb_image_resize`
  only packages are not enough).

Optional at build time (only if you ship the matching feature):

- **libwebp** for WebP wallpapers when appearance is synced from Noctalia Shell
  (other raster formats use vendored Wuffs).

### Vendored (no system package)

Shipped under `third_party/`: **Wuffs** (image decode). License file lives beside
the sources.

### Runtime

| Dependency | Role |
|---|---|
| **greetd** | Display manager; greeter runs as greetd's `default_session` |
| **D-Bus** | `noctalia-greeter-session` wraps the compositor in `dbus-run-session` |
| PAM (`/etc/pam.d/greetd`) | Authentication |
| Wayland sessions | `.desktop` files under `wayland-sessions` (niri, Hyprland, etc.) |
| wlroots 0.20 | `noctalia-greeter-compositor` (KMS, libinput, xkbcommon, wayland-server) |
| Mesa / EGL / GLES | Greeter client rendering (or epoxy where distros split packages that way) |
| Cairo, Pango, Fontconfig, FreeType, HarfBuzz, librsvg, GLib | Text and UI rendering |

Optional:

| Dependency | Role |
|---|---|
| **polkit** | `pkexec noctalia-greeter-apply-appearance` when Noctalia Shell syncs appearance |
| **libwebp** | WebP wallpaper decode for that sync path (Wuffs covers other raster formats) |

Optional pairing with **[Noctalia v5](https://github.com/noctalia-dev/noctalia)** for wallpaper/palette sync from shell settings (needs polkit + `noctalia-greeter-apply-appearance` on the sync path).

## greetd integration

greetd should run the **session wrapper**, not the greeter binary directly:

```toml
[default_session]
command = "/usr/bin/noctalia-greeter-session"
user = "greeter"
```

Use the real path from your package prefix. Optional pinned session:

```toml
command = "/usr/bin/noctalia-greeter-session -- --session niri"
```

`setup_greeter_system.sh` (installed under `share/noctalia-greeter/`) prints a ready-to-paste block and patches greetd PAM for `XDG_RUNTIME_DIR`. Packagers may wrap or invoke it from `postinst`.

Do **not** put environment assignments inside greetd TOML (`FOO=1 /path/...` is invalid). Use a plain path, or `env FOO=1 /path/...` in `command` if needed.

## Greeter session user resolution

Several tools need the Unix account greetd uses for the greeter session
(`noctalia-greeter-apply-appearance`, `setup_greeter_system.sh`, appearance sync
chown). Resolution is implemented in [`src/greeter/greetd_user.cpp`](src/greeter/greetd_user.cpp)
and exposed as `noctalia-greeter-apply-appearance --print-greeter-user`.

### Precedence

| Order | Source | When it applies |
|---|---|---|
| 1 | `GREETER_USER` | Explicit override (packagers, custom layouts) |
| 2 | Owner of the state dir | Dir exists and is owned by a non-root uid |
| 3 | Greetd config | `GREETD_CONFIG`, then `/etc/greetd/config.toml` |
| 4 | Fallback | First existing account named `greeter`, then `greetd` |

State dir path: `/var/lib/noctalia-greeter` by default, or `NOCTALIA_GREETER_STATE_DIR`.

Greetd config parsing looks for a session block whose `command` contains
`noctalia-greeter`, then falls back to `[default_session].user`. This is a
small hand parser, not full TOML.

### What packagers should do

**After install (recommended):** create the state dir and `chown` it to the same
user greetd runs the greeter as. Once that is done, step 2 wins and tools do not
need to read greetd config at all. This covers distros where greetd's live
config is not `/etc/greetd/config.toml`.

```sh
# Example postinst fragment (adjust user and paths)
greeter_user=greeter
state_dir=/var/lib/noctalia-greeter
install -d -m 0750 -o "${greeter_user}" -g "${greeter_user}" "${state_dir}"
```

Or run the shipped setup script as root (it resolves the user, prepares paths,
and installs default `greeter.toml`):

```sh
/usr/share/noctalia-greeter/setup_greeter_system.sh
```

**First install / greenfield:** ownership cannot work until the directory exists
and is chowned. Bootstrap uses greetd config (step 3) or `GREETER_USER`. We do
not try to discover greetd's config path from systemd units or other heuristics;
that is fragile and still misses custom layouts.

| Situation | Approach |
|---|---|
| Normal packaged install | `chown` state dir in `postinst` to session user |
| Nonstandard greetd config location | Set `GREETER_USER` for setup/sync tools, or `GREETD_CONFIG` if the file path is known |
| Manual setup on a new machine | Run `setup_greeter_system.sh` (reads standard greetd config paths) |
| NixOS | [`nix/nixos-module.nix`](nix/nixos-module.nix) wires user, tmpfiles, and greetd `command` |

### tmpfiles.d

Meson installs `lib/tmpfiles.d/noctalia-greeter.conf`, which recreates
`/var/lib/noctalia-greeter` as `greeter:greeter`. That drop-in is optional
convenience for systemd/opentmpfiles; it **hardcodes** the `greeter` user.

If your package uses a different session user, either override under
`/etc/tmpfiles.d/` or rely on `postinst` + `ensure_greeter_paths` from
`setup_greeter_system.sh` (portable across OpenRC, runit, systemd, etc.).

## System paths

| Path | Owner | Purpose |
|---|---|---|
| `/var/lib/noctalia-greeter/` | greetd session user (`0750`) | `greeter.toml`, `sync.toml`, wallpapers, synced layout |
| `/etc/greetd/config.toml` | root (readable by greeter user) | greetd session definition (bootstrap only for user resolution) |
| `/etc/pam.d/greetd` | root | PAM stack; setup script adds elogind/systemd runtime dir module |

Override state dir with `NOCTALIA_GREETER_STATE_DIR` (must match across greetd,
setup scripts, and `noctalia-greeter-apply-appearance`).

Logging under greetd defaults to **syslog** (journald / system logger). The
session wrapper parks stdout/stderr; use `NOCTALIA_GREETER_LOG=stderr` or a file
path for debugging.

## Polkit (optional)

Only needed for **Noctalia Shell appearance sync**. Login and local `greeter.toml`
config work without polkit.

Shell sync runs:

```text
pkexec noctalia-greeter-apply-appearance <staging-dir>
```

Policy ships as `org.noctalia.greeter.apply-appearance` (admin auth). The helper
installs synced files into the state dir and chowns them to the resolved greeter
user.

## What Noctalia Greeter is not

- Not a replacement for greetd itself.
- Not tied to a single distro's greetd config layout beyond the precedence table above.

## Contact

- Issues: https://github.com/noctalia-dev/noctalia-greeter/issues
- Discord: https://discord.noctalia.dev
