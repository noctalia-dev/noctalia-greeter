---
title: Troubleshooting
description: Find logs and resolve greeter startup, login, display, sync, and input problems.
sidebar:
  order: 6
---

# Troubleshooting

Start with the greetd and Noctalia Greeter logs. The message immediately before a blank screen, failed login, or failed sync is usually more useful than the visible symptom.

- [Logging](#logging)
- [Startup and display](#startup-and-display)
- [Login and session](#login-and-session)
- [Sync and Polkit](#sync-and-polkit)
- [Appearance and input](#appearance-and-input)

## Logging

Under greetd, Noctalia Greeter logs to **syslog** by default. On systemd the messages go to journald; OpenRC systems commonly use syslog-ng, metalog, or another system logger. The session wrapper parks stdout and stderr so wlroots and libseat messages do not flash on the virtual terminal before DRM takes over.

On systemd, inspect the current boot's greetd log and look for the `noctalia-greeter` identifier:

```sh
journalctl -u greetd -b | grep noctalia-greeter
```

The service may have a different unit name on a distribution-provided setup. On a non-systemd installation, use that system's greetd service log or system log.

`NOCTALIA_GREETER_LOG` changes the destination:

| Value | Effect |
|-------|--------|
| Unset | Log through syslog; keep stdout and stderr parked |
| `stderr` | Send informational and debug messages to stdout, and warnings and errors to stderr |
| An absolute file path | Append to that file and continue logging through syslog |

For temporary console diagnostics, add the variable to the greetd session command:

```toml
[default_session]
command = "env NOCTALIA_GREETER_LOG=stderr WLR_LOG=info /usr/bin/noctalia-greeter-session"
```

greetd does not interpret a bare environment assignment as a command, so keep the explicit `env`. Remove debug logging after diagnosing the problem.

## Startup and display

### Blank screen

Check the logs first. Confirm that `/var/lib/noctalia-greeter` exists, is mode `0750`, and is owned by the account configured as greetd's greeter user. Also verify that the greeter package's required `assets/` tree was installed.

For a source checkout, `sudo ./scripts/setup_greeter_system.sh` performs the complete system setup; `just setup-log-dir` repairs only the state directory. Those commands are available only from the source tree. Packaged installations should use their distribution's setup or reinstall procedure. See [Installation](installation.md#configure-greetd-manually).

### Black screen after reboot

Treat this like a blank screen: inspect the current-boot logs, confirm that the state directory was recreated with the right owner, and check that referenced wallpaper files still exist. A tmpfiles rule that assumes a different greeter account can recreate the directory with the wrong ownership.

### Failed to spawn client or wrong executable path

The greetd `command` must use the full installed path to `noctalia-greeter-session`, often `/usr/bin/noctalia-greeter-session` for a package and `/usr/local/bin/noctalia-greeter-session` for a manual install. Find it with:

```sh
command -v noctalia-greeter-session
```

Update `/etc/greetd/config.toml` as shown in [Configure greetd manually](installation.md#configure-greetd-manually).

### `WAYLAND_DISPLAY is not set`

greetd must launch `noctalia-greeter-session`, not the `noctalia-greeter` client directly. The session wrapper starts the bundled compositor, which creates `WAYLAND_DISPLAY` before launching the client. Correct the session command in `/etc/greetd/config.toml`.

### Wrong size or only one monitor looks right

Check that `[output].name` matches a connector reported by `noctalia-greeter outputs`. Review the configured layout and scales when using more than one monitor. See [Displays](displays.md).

### Blank flash or modeset during login

The greeter and desktop session may be selecting different DRM modes. Set `[output].width` and `[output].height` to the desktop's resolution; both values are required. See [Match the desktop output mode](displays.md#match-the-desktop-output-mode).

### Screen never blanks

Set `[idle].timeout` in `greeter.toml`. The default is `0`, which disables blanking. Pointer motion does not reset the timer while the display is on. See [Blank displays when idle](displays.md#blank-displays-when-idle).

### Blanked screen does not wake

Press a key, click a mouse button, or touch the screen. Pointer movement and scrolling should also wake an already blanked screen. If the outputs remain off, inspect the compositor log for idle or output-commit errors.

### Interface is too small or too large

Set `[output].scale` for one global UI scale, or `[output].scales` for connector-specific values. See [Scale the interface](displays.md#scale-the-interface).

### Display is stuck during development

From another TTY or an SSH session, a **source checkout only** provides:

```sh
just recover
```

This command terminates greeter and compositor processes and stops greetd, so use it only when recovering a development run. It is not installed by distribution packages.

## Login and session

### `Login service stopped responding`

greetd did not answer a request before the watchdog expired. Inspect the greetd journal for a stalled or crashed PAM or session worker, then restart greetd. `[auth].request_timeout` controls the watchdog; its default is `60` seconds, and `0` disables it. See [Configuration](configuration.md).

### Wrong session is selected on startup

Use the desktop entry's exact **`Name=`** value from `noctalia-greeter sessions`, not its `.desktop` filename. Command-line `--session` takes precedence over `[session].default`, which takes precedence over the last-used `[session].last` value in `sync.toml`.

Put names containing spaces or punctuation in `greeter.toml` instead of leaving them unquoted in the greetd command. See [Default session](configuration.md#default-session).

### GNOME returns to the greeter

GNOME expects a systemd-managed user session and may fail with a `graphical-session-pre.target` error. The greeter passes `XDG_SESSION_TYPE` and the desktop entry's `DesktopNames` environment through greetd, but GNOME support remains best-effort compared with GDM. If the normal entry still fails, use GDM for GNOME or create a suitable `wayland-sessions` wrapper.

## Sync and Polkit

### Appearance sync

If the synced look is missing, first check the installed versions. The
constrained `--sync` path requires Noctalia Greeter 1.4.0 or newer together
with the next Noctalia release after 5.0.1; current `-git` packages, `main`
checkouts, or manual builds from current `main` work when both projects are up
to date. Older and mixed-version combinations use the permanently supported
administrator-authenticated legacy positional path instead.

Confirm that `noctalia-greeter-apply-appearance` and an appropriate privilege
tool are installed. The constrained path also requires `pkexec` and the
packaged Polkit action. Run **Settings → Security → Noctalia Greeter → Sync
Now** again, then log out or restart greetd. See [Sync with Noctalia](sync.md).
Noctalia rejects a helper in a user-owned checkout or writable prefix; manual
builds must be installed into a root-owned, non-user-writable system prefix.

### No privilege escalator is available

The constrained `--sync` mode requires `pkexec` on `PATH`; direct `sudo`,
`doas`, and `run0` invocations are unsupported for that mode. On NixOS, enable
the Polkit `pkexec` wrapper. Without a passwordless rule, a working Polkit
authentication agent prompts for administrator authentication every time; this
is a supported permanent setup. The authenticated legacy positional mode may
use `run0`, `pkexec`, or Noctalia's configured privilege-command prefix. See
[Session and runtime requirements](sync.md#session-and-runtime-requirements)
for terminal alternatives.

### Passwordless sync still prompts

On a conventional packaged distribution, check whether the selected account is
present in the greeter-managed rule:

```sh
noctalia-greeter passwordless-sync status alice
```

Rerun status with `sudo` if the distribution restricts reads of its Polkit
rules directory.

If needed, add it with
`sudo noctalia-greeter passwordless-sync enable alice`. The installed Greeter
must be 1.4.0 or newer, the package must include the dedicated
`org.noctalia.greeter.sync-appearance` action, and Polkit must see the caller as
an active local session. A seatd-only session without logind or elogind cannot
match this rule.

On NixOS, inspect the evaluated `passwordless-sync-users` option or the
declarative `security.polkit.extraConfig` rule instead of relying on an
imperative `/etc` change. See [Authorization](sync.md#authorization) for both
setup models and the exact rule constraints.

To remove the account from the CLI-managed authorization, run:

```sh
sudo noctalia-greeter passwordless-sync disable alice
```

Administrator-prompted constrained sync and the permanently authenticated
legacy path remain available after disabling the rule. Prompts resume when no
separate administrator-authored Polkit rule also authorizes that account.

### Sync Now does nothing or the look remains unchanged

Noctalia first probes the helper's protocol. A compatible pair with the default
state directory stages appearance data and waits for Polkit to authorize
`noctalia-greeter-apply-appearance --sync <staging-dir>`; a recognized older
helper or a custom state directory uses the authenticated positional call
instead. An unrecognized or malformed probe response stops safely rather than
guessing a protocol. Inspect the Noctalia notification and logs for the probe,
staging, or authorization error. Noctalia warns when approval is still pending
after 90 seconds.

On a seatd-only session without logind or elogind, the graphical Polkit agent
cannot register. Run the terminal command for the applicable constrained or legacy
mode shown in [Session and runtime requirements](sync.md#session-and-runtime-requirements),
then log out to view the newly installed appearance.

### Polkit reports `No session for pid`

This is expected when using seatd without logind or elogind: Polkit has no active graphical session to associate with the process. Use terminal or console `pkexec` for an administrator-authenticated sync, or install elogind to provide session tracking. A passwordless rule limited to active local sessions cannot match a seatd-only session.

## Appearance and input

### User avatars are missing

Install and enable AccountsService, commonly provided by an `accountsservice` package and `accounts-daemon` service. The greeter reads each user's `IconFile` from AccountsService and displays a fallback when the service or image is unavailable. Avatars are not copied by appearance sync.

### Cursor theme is missing or falls back to the default

Set `[cursor].theme` and, optionally, `[cursor].size` in `greeter.toml`. If the theme is outside the default search path, also set `[cursor].path`. The path and theme files must be readable by the greetd user; a theme installed only in your home directory is normally unavailable. See [Cursor theme](input.md#cursor-theme).
