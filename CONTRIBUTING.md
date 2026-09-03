# Contributing to Noctalia Greeter

Thank you for contributing to Noctalia Greeter. It is a minimal login greeter for greetd, backed by a bundled wlroots
compositor and a native Wayland client. Read this guide with [README.md](README.md), which covers user-facing setup,
packaging, configuration, and troubleshooting.

## Scope and design

Noctalia Greeter owns the login screen, user and session selection, password authentication, appearance settings, and
optional appearance synchronization with Noctalia Shell. It is not a desktop shell or a general-purpose compositor.

Keep changes consistent with these principles:

- Prefer direct Wayland, wlroots, and greetd interfaces over additional toolkit dependencies.
- Keep compositor, greeter, authentication, and rendering responsibilities in their existing domains.
- Treat logical output geometry and fractional scaling as separate from buffer dimensions.
- Keep configuration names canonical. Do not add compatibility aliases, migration readers, or silent fallbacks.
- Keep the nested development path usable without a real greetd login session.

## Development setup

Install the build dependencies listed in [README.md](README.md). The normal development commands use [Just](https://just.systems/):

```sh
just build
just format
just format-check
just lint
```

`just build` configures and compiles the debug build in `build/`. `just format` changes C++ files, while
`just format-check` checks them without modifying the working tree. `just lint` runs cppcheck over `src/`.

Useful build variants and run targets:

```sh
just build-release
just build-asan
just run
just run-local
just run-niri
just run-asan
```

- `just run` starts the greeter inside the bundled compositor and a D-Bus session.
- `just run-local` starts a UI development session in the current login session with dummy users.
- `just run-niri` runs the greeter directly in an existing Wayland session for UI-only checks.
- `just run-asan` runs the nested compositor and greeter with AddressSanitizer enabled.

Do not use `just install`, `just setup-system`, or the greetd run targets for routine iteration. They modify system
installation or greetd state. Use them only when testing installation and real login behavior.

## Source layout

```text
src/
  accounts/   User account icons and avatar data
  compositor/ Bundled wlroots compositor
  config/     Shared configuration types
  core/       Logging, resources, deferred work, and UI phases
  greetd/     greetd IPC client
  greeter/    Login UI, sessions, preferences, appearance sync, and actions
  render/     OpenGL ES rendering, text, scene graph, and animation
  tools/      Small installed helper programs
  ui/         Reusable controls, palette, and style
  wayland/    Wayland client connection, seats, and output handling
assets/       Fonts, icons, and other runtime resources
data/        Installed service, policy, and tmpfiles files
scripts/      Installation, greetd, and development helpers
docs/user/   User-facing configuration documentation
examples/    Commented greeter configuration
```

Headers live beside their source files. Project headers use paths relative to `src/` where the existing code does so.
The bundled compositor is part of this repository and is built from `src/compositor/`; do not replace it with a system
compositor dependency.

## Testing changes

Choose verification that exercises the behavior being changed:

- Build changes: `just build` and, when relevant, `just build-release`.
- Rendering or layout changes: `just run` or `just run-local`, with visual inspection at the affected output sizes.
- Authentication or session changes: test the real greetd flow with `just run-greeter` or an installed greetd setup.
- Appearance synchronization: test `Sync Now` against a running Noctalia Shell and verify the resulting greeter state.
- Output changes: test multiple monitors, scaled outputs, a pinned output, and custom output layout or transforms when
  those paths are affected.
- NixOS module changes: build the flake or module and test the generated greeter configuration.
- Logging changes: use `just log-test` and confirm the expected stderr or log-file behavior.

A compositor session can affect the user's display. Before testing an installed greetd setup or any command that may
interrupt a live session, make the test environment explicit and keep a recovery terminal available. `just recover`
terminates the greeter processes and prints the next recovery step when a display is stuck.

## Code style

Run `just format` before committing. Keep warnings enabled in new or changed code and avoid broad warning suppressions.
Use the existing naming, ownership, and lifecycle patterns in the surrounding directory. Comments should explain the
current behavior or a non-obvious constraint, not record rejected alternatives.

For changes to configuration or user-visible behavior, update the relevant page under [`docs/user/`](docs/user/) and,
when appropriate, [`examples/greeter.toml`](examples/greeter.toml) in the same pull request. Keep installation and
runtime setup details in `README.md` when they apply to users rather than only contributors.

## Pull request process

Pull request descriptions are checked automatically when they are opened, edited, reopened, or marked ready for
review. Keep the `## Summary`, `## Motivation`, `## Type of Change`, `## Testing`, and `## Checklist` headings and the
Checklist wording from [`.github/PULL_REQUEST_TEMPLATE.md`](.github/PULL_REQUEST_TEMPLATE.md). The remaining sections
are context only: fill them in, leave them empty, or delete them. In Type of Change, keep only the lines that apply.

Draft pull requests may leave checkboxes incomplete. Before marking a pull request ready for review, select at least one
change type and check every item in the Checklist section. A pull request that is missing required template structure
is commented on and converted back to a draft; add the missing content and mark it ready for review to run the check
again. The check never closes a pull request.
