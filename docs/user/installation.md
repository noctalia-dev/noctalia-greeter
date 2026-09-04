---
title: Installation
description: Install Noctalia Greeter from distribution packages, NixOS, or source and connect it to greetd.
sidebar:
  order: 1
---

# Installation

Noctalia Greeter is available from these package sources:

- [Arch Linux (AUR)](#arch-linux)
- [CachyOS](#cachyos)
- [KaOS](#kaos)
- [Fedora](#fedora)
- [Debian and Ubuntu](#debian-and-ubuntu)
- [NixOS](#nixos)

You can also [build it manually](#manual-installation) on another Linux distribution.

:::note[Package ownership]
The Noctalia team maintains the source build, project flake, and AUR packages. Distribution packages and packages from other repositories follow their maintainers' own packaging processes. Review a third-party repository before installing from it.
:::

Every installation needs **greetd** and **D-Bus** on the machine where the greeter runs. The **Polkit daemon and `pkexec`** are optional for login, but required to sync appearance from Noctalia; some distributions package `pkexec` separately.

:::note[Sync compatibility]
Passwordless sync is optional and requires Noctalia Greeter 1.4.0 or newer
together with the next Noctalia release after 5.0.1. Current `-git` packages,
`main` checkouts, or manual builds from current `main` work when both projects
are up to date. Older and mixed-version combinations continue to use the
permanently supported administrator-authenticated legacy path. Without a
site-local Polkit allow rule, sync requests administrator authentication every
time.
:::

After installing a compatible package on a distribution with a conventional
mutable `/etc`, passwordless sync can be enabled for a selected login account
with:

```sh
sudo noctalia-greeter passwordless-sync enable alice
```

This is optional; without it, administrator-prompted sync remains fully
supported. Use
`sudo noctalia-greeter passwordless-sync disable alice` to undo it. NixOS users
declare the equivalent users in their system configuration instead. See
[Sync with Noctalia](sync.md#authorization) for status commands, NixOS examples,
the manual Polkit alternative, and security constraints.

For user avatars in the login picker, optionally install and enable **accountsservice** (usually the `accounts-daemon` service). Noctalia Greeter reads each user's avatar from `org.freedesktop.Accounts` and shows a fallback when the service is unavailable or no `IconFile` is set.

Desktop sessions such as niri or Hyprland are separate packages; install them as you normally would.

## Package sources

### Arch Linux

Tagged releases are available as [`noctalia-greeter`](https://aur.archlinux.org/packages/noctalia-greeter) in the AUR. Install it with an AUR helper of your choice, for example:

```sh
paru -S noctalia-greeter
```

To follow development snapshots from `main`, install [`noctalia-greeter-git`](https://aur.archlinux.org/packages/noctalia-greeter-git) instead.

### CachyOS

Noctalia Greeter is available from the [official CachyOS repository](https://packages.cachyos.org/package/cachyos/x86_64/noctalia-greeter):

```sh
sudo pacman -Syu noctalia-greeter
```

### KaOS

Noctalia Greeter is available from the [KaOS `apps` repository](https://kaosx.us/packages/packages.php?exact=1&search=noctalia-greeter):

```sh
sudo pacman -Syu noctalia-greeter
```

### Fedora

For Fedora 44 and newer, tagged releases are available from the community-maintained [Terra repository](https://terrapkg.com). Add Terra, then install the greeter:

```sh
sudo dnf install --nogpgcheck --repofrompath 'terra,https://repos.fyralabs.com/terra$releasever' terra-release
sudo dnf install noctalia-greeter
```

Automated development snapshots are also available from the [LionHeartP Copr](https://copr.fedorainfracloud.org/coprs/lionheartp/Hyprland/). The snapshot package is being renamed to distinguish it from stable builds, so check the Copr package page for its current name. Fedora's default repositories do not currently carry Noctalia Greeter.

### Debian and Ubuntu

The community-maintained Noctalia APT repository provides `noctalia-greeter` for Debian Trixie, Debian Sid, and Ubuntu 26.04. Follow the shared [Debian repository setup instructions](https://docs.noctalia.dev/noctalia/getting-started/installation/#debian), then install:

```sh
sudo apt update
sudo apt install noctalia-greeter
```

### NixOS

Noctalia Greeter is packaged in [nixpkgs unstable](https://search.nixos.org/packages?channel=unstable&show=noctalia-greeter&query=noctalia-greeter). The nixpkgs module installs the package, enables greetd, Polkit, and AccountsService, and configures the greeter session:

```nix
services.displayManager.noctalia-greeter = {
  enable = true;
  settings = {
    cursor.size = 24;
    keyboard.layout = "us";
  };
  cursorTheme = {
    package = pkgs.bibata-cursors;
    name = "Bibata-Modern-Ice";
  };
};
```

The project flake is an alternative for channels that do not yet contain the package or for following the greeter repository directly. Add it to `flake.nix`:

```nix
inputs.noctalia-greeter = {
  url = "github:noctalia-dev/noctalia-greeter";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

Import `inputs.noctalia-greeter.nixosModules.default`, then enable the project module:

```nix
programs.noctalia-greeter = {
  enable = true;

  # Optional: passwordless appearance sync for selected active local users.
  # Leave empty or omit to require an administrator prompt for every sync.
  passwordless-sync-users = [ "alice" ];

  # Optional: extra flags after `--` on noctalia-greeter-session.
  greeter-args = "";

  # Full declarative greeter.toml, overwritten on each activation.
  settings = {
    cursor = {
      theme = "Bibata-Modern-Ice";
      size = 24;
      path = "${pkgs.bibata-cursors}/share/icons";
    };
  };
};
```

The project module enables greetd, AccountsService, Polkit, and the `pkexec`
wrapper (where that wrapper option exists), and configures greetd to launch the
packaged `noctalia-greeter-session`. This keeps the normal administrator-prompted
sync available. When `passwordless-sync-users` is non-empty, the module also adds
a Polkit rule limited to those users in active local sessions.

Leaving `passwordless-sync-users` unset or empty keeps the packaged policy's
administrator prompt for every constrained sync.

Because NixOS owns the generated Polkit configuration declaratively, prefer the
module option or an equivalent `security.polkit.extraConfig` rule over the
imperative `passwordless-sync` CLI on NixOS.

The modules use different option paths and cursor-theme interfaces: nixpkgs uses `services.displayManager.noctalia-greeter` and `cursorTheme.package`, while the project module uses `programs.noctalia-greeter` and writes cursor `theme` and `path` through `settings`. Enable only one module.

See [Configuration](configuration.md) for all settings and [Sync with Noctalia](sync.md) for passwordless sync details.

### Manual installation

If no package is available for your distribution, follow the repository's [build-from-source instructions](https://github.com/noctalia-dev/noctalia-greeter#build-from-source). After installing from source, run the setup script as root so `/var/lib/noctalia-greeter/` and `greeter.toml` are created for the greetd user:

```sh
sudo ./scripts/setup_greeter_system.sh
```

For a manual build that will use passwordless sync, configure Meson with
`--prefix=/usr`. Standard Polkit installations load actions from
`/usr/share/polkit-1/actions`; a default `/usr/local` build can run the login
greeter and retain administrator-authenticated sync, but its dedicated action
is not normally discovered.

Packaged integrations may perform this setup automatically. Continue below to verify the greetd session command on your system.

## Configure greetd manually

Point greetd at the installed session wrapper. Find its path rather than assuming `/usr/local`:

```sh
command -v noctalia-greeter-session
```

For example, a manual installation under `/usr/local` uses:

```toml
[default_session]
command = "/usr/local/bin/noctalia-greeter-session"
user = "greeter"
```

Replace the command with the path reported on your system, commonly `/usr/bin/noctalia-greeter-session` for distribution packages. Set `user` to the account that runs greetd's greeter session. The included system setup script prints a ready-to-paste `config.toml` block using the path and user it detects.

The greetd `command` is not interpreted as a shell command. To set environment variables, invoke `env` explicitly:

```toml
command = "env WLR_LOG=info /usr/bin/noctalia-greeter-session"
```

For selecting the initial desktop session or user, see [Configuration](configuration.md).

## Restart greetd

Restart greetd after changing `/etc/greetd/config.toml`.

On systemd:

```sh
sudo systemctl restart greetd
```

On runit:

```sh
sudo sv restart greetd
```
