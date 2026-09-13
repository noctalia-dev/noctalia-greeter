---
title: Installation
description: Install Noctalia Greeter from distribution packages, NixOS, or source and connect it to greetd.
sidebar:
  order: 1
---

For most users, the shortest path is to install a distribution package, point
greetd at the installed session wrapper, and then switch display managers. The
steps below put those changes in the order they should be made.

## 1. Check prerequisites and choose an installation method

Every installation needs **greetd** and **D-Bus** on the machine where the
greeter runs. Install desktop sessions such as niri or Hyprland separately.

Choose the method for your system:

- [Install a distribution package](#2-install-noctalia-greeter) on Arch Linux,
  CachyOS, KaOS, Fedora, Debian, or Ubuntu.
- [Build from source](building-from-source.md) if no package is available.
- Follow the separate [NixOS declarative setup](#nixos-declarative-setup) on
  NixOS. Do not follow the imperative service steps in sections 3–6.

Sections 3–6 assume systemd. With another init system, configure greetd in
section 3, then skip to [Other init systems](#other-init-systems).

:::note[Package ownership]
The Noctalia team maintains the source build, project flake, and AUR packages.
Distribution packages and packages from other repositories follow their
maintainers' own packaging processes. Review a third-party repository before
installing from it.
:::

## 2. Install Noctalia Greeter

Use one of the package options below. After it finishes, continue to
[Configure greetd](#3-configure-greetd).

### Arch Linux

Tagged releases are available as
[`noctalia-greeter`](https://aur.archlinux.org/packages/noctalia-greeter) in the
AUR. Install it with an AUR helper of your choice, for example:

```sh
paru -S noctalia-greeter
```

To follow development snapshots from `main`, install
[`noctalia-greeter-git`](https://aur.archlinux.org/packages/noctalia-greeter-git)
instead.

### CachyOS

Noctalia Greeter is available from the
[official CachyOS repository](https://packages.cachyos.org/package/cachyos/x86_64/noctalia-greeter):

```sh
sudo pacman -Syu noctalia-greeter
```

### KaOS

Noctalia Greeter is available from the
[KaOS `apps` repository](https://kaosx.us/packages/packages.php?exact=1&search=noctalia-greeter):

```sh
sudo pacman -Syu noctalia-greeter
```

### Fedora

For Fedora 44 and newer, tagged releases are available from the
community-maintained [Terra repository](https://terrapkg.com). Add Terra, then
install the greeter:

```sh
sudo dnf install --nogpgcheck --repofrompath 'terra,https://repos.fyralabs.com/terra$releasever' terra-release
sudo dnf install noctalia-greeter
```

Automated development snapshots are also available from the
[LionHeartP Copr](https://copr.fedorainfracloud.org/coprs/lionheartp/Hyprland/).
The snapshot package is being renamed to distinguish it from stable builds, so
check the Copr package page for its current name. Fedora's default repositories
do not currently carry Noctalia Greeter.

### Debian and Ubuntu

The community-maintained Noctalia APT repository provides `noctalia-greeter`
for Debian Trixie, Debian Sid, and Ubuntu 26.04. Follow the shared
[Debian repository setup instructions](https://docs.noctalia.dev/noctalia/getting-started/installation/#debian),
then install:

```sh
sudo apt update
sudo apt install noctalia-greeter
```

## 3. Configure greetd

greetd must launch **`noctalia-greeter-session`**, not the
`noctalia-greeter` executable. Find the wrapper installed on your system:

```sh
command -v noctalia-greeter-session
```

Open `/etc/greetd/config.toml` and merge the following values into its existing
`[default_session]` section. Do not blindly overwrite an existing greetd
configuration:

```toml
[default_session]
command = "/usr/bin/noctalia-greeter-session"
user = "greeter"
```

Replace `command` with the full path reported above. Distribution packages
commonly install it under `/usr/bin`; the path may differ for another prefix.
Keep the existing greeter account if your configuration uses a user other than
`greeter`.

Some packages configure greetd automatically. In that case, verify that the
existing `command` points to `noctalia-greeter-session` and leave the rest of
the configuration intact.

## 4. Replace the current display manager safely

:::caution
Changing display managers can stop the graphical login immediately. Before
continuing, switch to a TTY or keep another root-capable shell open so you can
recover if greetd does not start.
:::

On a systemd distribution, first identify the display manager currently behind
the `display-manager.service` alias:

```sh
systemctl status display-manager.service
```

Service unit names vary. The actual unit may be `gdm.service`, `sddm.service`,
`lightdm.service`, or something else. Disable the unit shown on your system;
do not copy one of these examples without checking:

```sh
sudo systemctl disable --now UNIT.service
```

If no other display manager is installed or enabled, there is nothing to
disable. Do not leave another display manager enabled alongside greetd: both
can compete for the login screen on the next boot.

## 5. Enable greetd

After the previous display manager is disabled, enable and start greetd:

```sh
sudo systemctl enable --now greetd
```

## 6. Reboot and verify

Reboot to confirm greetd starts cleanly:

```sh
sudo reboot
```

The Noctalia Greeter login screen should appear and list the desktop sessions
installed on the system. If it does not, return to the TTY or root shell you
kept available and inspect greetd:

```sh
systemctl status greetd
```

See [Troubleshooting](troubleshooting.md) for recovery steps and logging
details.

## NixOS declarative setup

NixOS users should configure both the package and display-manager selection in
their system configuration. Do not use the imperative systemd cutover from
sections 4–5. Keep a TTY or another root-capable shell available while applying
the change. Remove or disable any other display-manager module in the same
configuration, enable exactly one Noctalia Greeter module, and rebuild the
system through the normal NixOS workflow.

### nixpkgs module

Noctalia Greeter is packaged in
[nixpkgs unstable](https://search.nixos.org/packages?channel=unstable&show=noctalia-greeter&query=noctalia-greeter).
The nixpkgs module installs the package, enables greetd, Polkit, and
AccountsService, and configures the greeter session:

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

### Project flake module

The project flake is an alternative for channels that do not yet contain the
package or for following the greeter repository directly. Add it to
`flake.nix`:

```nix
inputs.noctalia-greeter = {
  url = "github:noctalia-dev/noctalia-greeter";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

Import `inputs.noctalia-greeter.nixosModules.default`, then enable the project
module:

```nix
services.displayManager.noctalia-greeter = {
  enable = true;
  settings = {
    cursor = {
      theme = "Bibata-Modern-Ice";
      size = 24;
      path = "${pkgs.bibata-cursors}/share/icons";
    };
  };
};
```

The project module enables greetd, AccountsService, and Polkit, and configures
greetd to launch the packaged `noctalia-greeter-session`. The two modules use
different option paths and cursor-theme interfaces, so enable only one.

Rebuild and reboot through the normal NixOS workflow to verify that the greeter
starts and lists the installed desktop sessions.

## Build from source

If no package is available for your distribution, follow
[Building from source](building-from-source.md). After installing, return to
[Configure greetd](#3-configure-greetd) to complete the setup.

## Other init systems

Disable any other display manager through your init system before enabling its
greetd service. Service setup varies by distribution.

On runit, restart greetd after changing `/etc/greetd/config.toml`:

```sh
sudo sv restart greetd
```

On systemd, restart greetd after later configuration changes with:

```sh
sudo systemctl restart greetd
```

## Optional next steps

- Install and enable **accountsservice** (usually the `accounts-daemon`
  service) to show user avatars. A fallback appears when it is unavailable or
  no `IconFile` is set.
- Adjust the greeter in [Configuration](configuration.md), including the
  initial desktop session or user.
- Set up optional appearance sharing in
  [Sync with Noctalia](sync.md). That guide is the source of truth for
  compatibility, authorization, and security details.
- See [Displays](displays.md) and [Input and keyboard](input.md) for
  platform-specific customization.
