---
title: Sync with Noctalia
description: Sync appearance and displays securely, including optional passwordless Polkit authorization.
sidebar:
  order: 3
---

# Sync with Noctalia

Noctalia Shell can copy its current appearance and monitor arrangement to the login screen. Install both **Noctalia** and **Noctalia Greeter**, including the packaged apply helper and Polkit action.

:::caution[Passwordless version requirement]
Passwordless sync requires **Noctalia Greeter 1.4.0 or newer** together with
**the next Noctalia release after 5.0.1**. Current `-git` packages or current
manual builds from `main` also work when both components are up to date.

If either component is older, Sync automatically uses the retained
administrator-authenticated legacy path. It never grants passwordless access
to that path.
:::

Noctalia only elevates a helper installed as a root-owned regular executable
through a root-owned, non-user-writable directory chain. This applies to both
prompted and passwordless sync. A helper in a user checkout or build directory
is intentionally rejected; install a manual build into a trusted system prefix.

- [What Sync copies](#what-sync-copies)
- [Run Sync](#run-sync)
- [Synced state and precedence](#synced-state-and-precedence)
- [Authorization](#authorization)
  - [Conventional packaged distributions](#conventional-packaged-distributions)
  - [NixOS](#nixos)
  - [Manual Polkit rule](#manual-polkit-rule)
- [Session and runtime requirements](#session-and-runtime-requirements)

## What Sync copies

- wallpaper, including per-output wallpapers
- palette and theme mode
- corner radius scale (`[shell] corner_radius_scale`)
- shell font (`[shell] font_family`)
- monitor layout, orientation, and effective per-output scale

The constrained `--sync` operation deliberately excludes session power
commands and custom session actions. Existing session commands and other
unrelated values in `sync.toml` are preserved. The authenticated compatibility
path retains the older payload, including Noctalia's configured session power
commands and actions, because older greeters expect it.

User avatars are also separate from appearance sync. They come from AccountsService, so enable `accountsservice` or `accounts-daemon` if you want the avatar selected in Noctalia to appear in the greeter.

The synced font must be available to the greeter account through Fontconfig. System-installed fonts work; fonts installed only in your home directory often do not.

## Run Sync

Open **Settings → Security → Noctalia Greeter → Sync Now**. Log out or restart greetd to see the result on the login screen.

Enable **Auto-Sync Greeter** on the same settings page to sync whenever the wallpaper, palette, theme mode, or shell font changes. Rapid changes are debounced into one operation. A corner-radius-only change still needs **Sync Now**, unless it arrives with another auto-sync trigger.

Only one sync runs at a time. An overlapping settings or auto-sync request leaves the active operation untouched; an overlapping `greeter-sync` IPC request reports that a sync is already in progress.

Noctalia checks the installed helper's capability before staging or requesting
authorization. A compatible helper selects constrained sync; a recognized
older helper selects the legacy path. An unrecognized response stops the
operation. A failed or cancelled constrained sync is never retried through the
legacy path.

## Synced state and precedence

Sync installs wallpaper files under `/var/lib/noctalia-greeter/` and merges appearance and output values into `/var/lib/noctalia-greeter/sync.toml`. It never overwrites declarative `greeter.toml`.

When the same value exists in both files, `greeter.toml` wins. In particular, a complete `[appearance.palette]` in `greeter.toml` selects that entire declarative appearance, including its wallpaper settings, ahead of the appearance in `sync.toml`.

With a current Shell and greeter:

| On disk / in config | Purpose |
|---------------------|---------|
| `wallpaper` / `wallpaper.<ext>` and `[appearance.wallpaper]` | Default image or fallback |
| `wallpaper-<connector>.*` and `[appearance.wallpapers.<connector>]` | Per-output wallpaper, such as `DP-2` |
| `[appearance.palette]`, `theme_mode`, `corner_radius_scale`, `font_family` | Synced colors and UI styling |
| `[output].layout`, `transforms`, `scales` | Synced monitor arrangement |

Each greeter view uses the wallpaper for its connector when one exists, then falls back to `[appearance.wallpaper]`. A connector pinned with `[output].name` uses its matching entry. See [Displays](displays.md) for connector and layout settings.

You do not need to add wallpaper keys to `greeter.toml` for Sync. To override them declaratively, use an absolute image path or `color:#RRGGBB`; `fill_mode` accepts `center`, `crop`, `fit`, `stretch`, or `repeat`:

```toml
[appearance.wallpaper]
path = "/var/lib/noctalia-greeter/wallpaper.webp"
fill_mode = "crop"

[appearance.wallpapers.DP-2]
path = "/var/lib/noctalia-greeter/wallpaper-DP-2.webp"
fill_mode = "crop"
```

The greeter exposes the **Synced** scheme when either config file contains a complete palette. Session and scheme choices made on the login screen are also remembered in `sync.toml`; see [Configuration](configuration.md#keys-the-greeter-remembers).

Legacy live `appearance.json` is migrated into `sync.toml` once and is no longer the source of truth.

## Authorization

The packaged `org.noctalia.greeter.sync-appearance` action matches only:

```text
noctalia-greeter-apply-appearance --sync <staging-directory>
```

It requires administrator authentication by default. Setup and legacy helper modes use generic administrator authorization and are never covered by the passwordless sync rule.

Passwordless authorization is entirely optional. If you do not install a
site-local rule, the constrained action continues to show an administrator
prompt for every sync. That authenticated workflow is a supported long-term
mode, not a migration step that users are expected to replace.

For the normal authenticated flow, a Polkit authentication agent must be
running in the desktop session. An existing desktop agent is enough. On a
minimal compositor with logind or elogind, enable **Settings → Security →
Polkit Agent** in Noctalia. A compatible passwordless rule removes the prompt
for its selected users, so it does not need an agent for this action.

:::caution[Check the installed policy]
Only add a passwordless rule when the helper supports `--sync` and its installed
Polkit action contains `org.freedesktop.policykit.exec.argv1` set to `--sync`.
Upstream releases through 1.3.1 use the old
`org.noctalia.greeter.apply-appearance` action, scoped only by executable path.
Never adapt the rule below to that action; doing so would also authorize the
legacy helper mode. If unsure, keep the administrator prompt.
:::

### Conventional packaged distributions

On distributions with a conventional mutable `/etc`, the installed greeter can
manage its narrow Polkit allow rule for you. Pass each local login account that
should be allowed to sync without a prompt:

```sh
sudo noctalia-greeter passwordless-sync enable alice
```

Enabling another account adds it without removing accounts that are already
allowed. Check the managed rule for one account, or list all allowed accounts:

```sh
noctalia-greeter passwordless-sync status alice
noctalia-greeter passwordless-sync status
```

If your distribution restricts reads of the Polkit rules directory, rerun the
status command with `sudo`.

To remove an account from the CLI-managed authorization:

```sh
sudo noctalia-greeter passwordless-sync disable alice
```

The command manages only the dedicated constrained appearance-sync action. It
does not grant passwordless access to the legacy positional helper, does not
create a general passwordless `sudo` rule, and does not turn on Noctalia's
**Auto-Sync Greeter** setting. Running `disable` removes the selected account's
managed authorization; prompted sync remains available.
If another administrator-authored Polkit rule also allows that account, remove
or update that separate rule before prompts resume.

Use these commands with a packaged or system-installed Greeter 1.4.0 or newer.
They intentionally reject an untrusted helper installation rather than writing
a rule for a checkout, build directory, or user-writable prefix. Packages must
also include the dedicated `org.noctalia.greeter.sync-appearance` Polkit action.

### NixOS

NixOS users configure the same authorization declaratively. With the project
module, list trusted login users directly:

```nix
programs.noctalia-greeter.passwordless-sync-users = [ "alice" ];
```

The option defaults to an empty list, which keeps every sync authenticated. The
module enables Polkit and the `pkexec` wrapper (where that wrapper option exists)
so this prompted flow is usable regardless of the list. When users are listed,
it additionally generates a rule limited to the exact packaged helper, the root
target account, and those users in active local sessions.

The nixpkgs module does not currently have that convenience option. Once its
selected greeter package meets the compatibility requirements above, add the
equivalent rule in your NixOS configuration:

```nix
security.polkit = {
  enable = true;
  extraConfig = ''
    polkit.addRule(function(action, subject) {
      var allowedUsers = ["alice"];

      if (action.id == "org.noctalia.greeter.sync-appearance" &&
          action.lookup("program") == "${pkgs.noctalia-greeter}/bin/noctalia-greeter-apply-appearance" &&
          action.lookup("user") == "root" &&
          subject.local && subject.active &&
          allowedUsers.indexOf(subject.user) >= 0) {
        return polkit.Result.YES;
      }
    });
  '';
};
```

Ensure `pkexec` is available on the selected NixOS release. On releases that
expose `security.polkit.enablePkexecWrapper`, set that option to `true`; releases
without it provide the wrapper when Polkit is enabled.

If you override `services.displayManager.noctalia-greeter.package`, use that
same package in the helper path. Greeter 1.3.1 and older do not provide this
action, so the rule has no effect with those packages. Do not change it to
their path-only `org.noctalia.greeter.apply-appearance` action.

### Manual Polkit rule

The CLI above is the normal setup on conventional packaged distributions. You
can instead maintain the rule yourself when integrating a custom package or
when you prefer to audit and own the complete policy. Confirm that the installed
package meets the compatibility requirements above; do not add this rule to an
upstream greeter 1.3.1 or older. The CLI's `status` command reports only the
authorization managed by that CLI, not arbitrary administrator-authored rules.

First find the helper's canonical path:

```sh
readlink -f "$(command -v noctalia-greeter-apply-appearance)"
```

The resulting helper must satisfy the trusted-install requirement above. Do not
point a passwordless rule at an executable inside a user checkout, build
directory, home directory, or other user-writable prefix.

Then create `/etc/polkit-1/rules.d/49-noctalia-greeter-sync.rules` as root,
mode `0644`. Replace `alice` and
`/usr/bin/noctalia-greeter-apply-appearance` with the trusted user and the path
printed above:

```js
polkit.addRule(function(action, subject) {
  var allowedUsers = ["alice"];

  if (action.id == "org.noctalia.greeter.sync-appearance" &&
      action.lookup("program") == "/usr/bin/noctalia-greeter-apply-appearance" &&
      action.lookup("user") == "root" &&
      subject.local && subject.active &&
      allowedUsers.indexOf(subject.user) >= 0) {
    return polkit.Result.YES;
  }
});
```

Polkit normally notices rule changes automatically. Remove this file to remove
this rule's authorization. Prompts resume for a user only when no other Polkit
rule authorizes the action. Do not install a blanket `YES` rule or a
passwordless `sudo` rule for the general helper.

## Session and runtime requirements

Passwordless authorization requires Polkit to recognize the caller as an active local session. A normal systemd-logind or elogind session provides this. A seatd-only session without either does not satisfy the rule.

The constrained helper also requires:

- invocation through `pkexec`, which supplies the verified `PKEXEC_UID`; direct `sudo`, `doas`, and `run0` calls are unsupported
- the exact caller-owned staging path `/run/user/<uid>/noctalia-greeter-sync`
- a protected logind/elogind-style `/run/user/<uid>` runtime directory
- the standard `/var/lib/noctalia-greeter` state directory, owned by the non-root greeter account

`--sync` ignores `NOCTALIA_GREETER_STATE_DIR`. Installations using a custom
state directory must use an absolute path and keep the legacy helper mode
administrator-authenticated. Noctalia forwards the custom path explicitly when
it launches that mode.

On seatd without logind, **Sync Now** can still stage the files, but authorization needs a terminal or console. Run:

```sh
greeter_sync_helper="$(readlink -f "$(command -v noctalia-greeter-apply-appearance)")"
greeter_sync_staging="/run/user/$(id -u)/noctalia-greeter-sync"
pkexec "$greeter_sync_helper" --sync "$greeter_sync_staging"
```

To open that prompt in a terminal from the desktop, configure a terminal wrapper. The helper path, `--sync`, and staging directory are appended automatically:

```toml
[shell.greeter_sync]
privilege_command = "ghostty -e pkexec"
```

The wrapper must ultimately invoke `pkexec`. Alternatively, install elogind so Polkit can attach an in-session authentication prompt. See [Troubleshooting](troubleshooting.md#appearance-sync) if Sync still fails.

In legacy mode—either with a recognized older helper or a custom greeter state
directory—the staged directory instead follows `$XDG_RUNTIME_DIR` and the
helper uses its positional syntax. For the default state directory, run:

```sh
pkexec noctalia-greeter-apply-appearance "$XDG_RUNTIME_DIR/noctalia-greeter-sync"
```

For a custom state directory, preserve it explicitly across `pkexec`:

```sh
pkexec /usr/bin/env NOCTALIA_GREETER_STATE_DIR=/absolute/custom/path \
  noctalia-greeter-apply-appearance "$XDG_RUNTIME_DIR/noctalia-greeter-sync"
```

The legacy path may also use `run0` or a configured administrator privilege
command. It always requires authentication. When `privilege_command` is used
with constrained sync, it must ultimately invoke `pkexec`; with a recognized
older greeter it retains the previous escalator behavior.
