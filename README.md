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

It is built for **greetd**: greetd launches `noctalia-greeter-session`, which starts the bundled wlroots compositor and runs the greeter inside that session.

Pair it with **[Noctalia v5](https://github.com/noctalia-dev/noctalia)** if you want wallpaper and palette synced from the shell settings (optional).

## Get started

Follow the **[installation guide](docs/user/installation.md)** for distribution
packages, NixOS, greetd configuration, and a safe handoff from another display
manager. If no package is available, use the separate
**[source-build guide](docs/user/building-from-source.md)**, then return to the
main installation flow.

After installation:

- [Configure the greeter](docs/user/configuration.md)
- [Set up optional Sync with Noctalia](docs/user/sync.md)
- [Troubleshoot a login or display problem](docs/user/troubleshooting.md)

### UI controls

To hide the session picker, set `appearance.hide_session_selector = true` in `greeter.toml`.
The selected session still follows CLI default, configured default, and the last
session from `sync.toml`; if none is available, the first discovered session is used.

Individual UI controls can be hidden under `[appearance]`; omitted options default to `false`:

```toml
[appearance]
hide_theme_selector = true
hide_shutdown_button = true
hide_reboot_button = false
hide_firmware_button = false
```

## Scope

Noctalia Greeter is a **display/login greeter** for greetd. It handles user/session selection and authentication UI.

It is **not** a desktop shell or compositor replacement.

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contributor guidance.

---

## License

Distributed under the MIT License. See [LICENSE](LICENSE) for details.
