project('noctalia-greeter', ['c', 'cpp'],
  version: files('VERSION'),
  meson_version: '>=1.1',
  default_options: [
    'cpp_std=c++23',
    'cpp_extensions=false',
    'warning_level=0',
    'b_ndebug=if-release',
    'c_args=-Wno-error=switch',
  ],
)

cc  = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

if get_option('buildtype') == 'release'
  add_project_arguments(
    '-fomit-frame-pointer', '-ffunction-sections', '-fdata-sections',
    language: ['c', 'cpp'],
  )
  add_project_link_arguments(
    '-Wl,--gc-sections', '-Wl,--as-needed',
    language: ['c', 'cpp'],
  )
endif

if get_option('native_optimizations')
  add_project_arguments('-march=native', '-mtune=native', language: ['c', 'cpp'])
endif

wayland_client_dep = dependency('wayland-client')
wayland_protos_dep = dependency('wayland-protocols')
xkbcommon_dep      = dependency('xkbcommon')
freetype2_dep      = dependency('freetype2')
fontconfig_dep     = dependency('fontconfig')
cairo_dep          = dependency('cairo')
cairo_ft_dep       = dependency('cairo-ft')
pango_dep          = dependency('pango')
pangocairo_dep     = dependency('pangocairo')
pangoft2_dep       = dependency('pangoft2')
librsvg_dep        = dependency('librsvg-2.0')
libxml2_dep        = dependency('libxml-2.0')
glib_dep           = dependency('glib-2.0')
gobject_dep        = dependency('gobject-2.0')
gio_dep            = dependency('gio-2.0')
wayland_server_dep = dependency('wayland-server')
tomlplusplus_dep   = dependency('tomlplusplus')
nlohmann_dep       = dependency('nlohmann_json')
wlroots_dep = dependency('wlroots-0.20')
libinput_dep = dependency('libinput')

egl_dep = dependency('egl', required: false)
gles2_dep = dependency('glesv2', required: false)
wayland_egl_dep = dependency('wayland-egl', required: false)

if not egl_dep.found()
  egl_dep = dependency('epoxy')
endif

if not gles2_dep.found()
  gles2_dep = dependency('epoxy')
endif

# ── System: stb_image_resize2 (header-only) ─────────────────────────────────
if not cc.has_header('stb/stb_image_resize2.h')
  error('stb/stb_image_resize2.h not found')
endif

libwebp_dep = dependency('libwebp', required: true)
libjxl_dep = dependency('libjxl', required: true)
libjxl_threads_dep = dependency('libjxl_threads', required: true)

# Unmodified, pinned noctalia-ui snapshot; builds without a shell checkout or network.
noctalia_ui_build_codecs = true
subdir('lib')

# Optional build provenance; source archives and builders without Git remain supported.
_git_program = find_program('git', required: false)
_git_revision_header_name = 'greeter_git_revision.h'
if _git_program.found()
  _git_revision_h = vcs_tag(
    input: 'src/greeter/git_revision.h.in',
    output: 'greeter_git_revision.h',
    command: [_git_program, 'describe', '--tags', '--always', '--dirty=-dirty', '--abbrev=12'],
    fallback: 'unknown',
  )
else
  # A distinct name avoids Meson cleaning the configured file as a stale vcs_tag
  # output when Git disappears between configurations of an existing build.
  _git_revision_header_name = 'greeter_git_revision_unknown.h'
  _git_revision_h = configure_file(
    input: 'src/greeter/git_revision.h.in',
    output: _git_revision_header_name,
    configuration: {'VCS_TAG': 'unknown'},
  )
endif

_greeter_sources = files(
  'src/config/output_identity.c',
  'src/time/time_format.cpp',
  'src/time/time_locale.cpp',
  'src/wayland/wayland_client.cpp',
  'src/wayland/greeter_output_metadata.cpp',
  'src/theme/builtin_palettes.cpp',
  'src/accounts/accounts_icon.cpp',
  'src/greetd/greetd_client.cpp',
  'src/greeter/appearance_sync.cpp',
  'src/greeter/appearance_config.cpp',
  'src/greeter/session_config.cpp',
  'src/greeter/greetd_user.cpp',
  'src/greeter/privileged_state_paths.cpp',
  'src/greeter/greeter_config_io.cpp',
  'src/greeter/greeter_layout.cpp',
  'src/greeter/greeter_preferences.cpp',
  'src/greeter/greeter_sessions.cpp',
  'src/greeter/greeter_model.cpp',
  'src/greeter/greeter_auth_controller.cpp',
  'src/greeter/greeter_surface.cpp',
  'src/greeter/greeter_window.cpp',
  'src/greeter/logind_resume.cpp',
  'src/greeter/power_actions.cpp',
  'src/greeter/greeter.cpp',
  'src/tools/passwordless_sync_policy.cpp',
  'src/tools/passwordless_sync_policy_xml.cpp',
  'src/greeter/logging.cpp',
  'src/greeter/resources.cpp',
  'src/greeter/library_runtime.cpp',
  'src/greeter/build_info.cpp',
  'src/greeter/ui/search_field.cpp',
  'src/greeter/ui/greeter_components.cpp',
  'src/greeter/render/wallpaper_program.cpp',
  'src/greeter/render/wallpaper_node.cpp',
) + [_git_revision_h]

_greeter_inc = [
  include_directories('src', 'lib'),
]

_greeter_dependencies = [
  noctalia_ui_codecs_dep,
  wayland_client_dep,
  egl_dep,
  gles2_dep,
  wayland_egl_dep,
  xkbcommon_dep,
  freetype2_dep,
  fontconfig_dep,
  cairo_dep,
  cairo_ft_dep,
  pango_dep,
  pangocairo_dep,
  pangoft2_dep,
  librsvg_dep,
  libxml2_dep,
  glib_dep,
  gobject_dep,
  gio_dep,
  libwebp_dep,
  libjxl_dep,
  libjxl_threads_dep,
  nlohmann_dep,
  tomlplusplus_dep,
]

noctalia_greeter_executable = executable('noctalia-greeter',
  sources: _greeter_sources + files('src/main.cpp'),
  dependencies: _greeter_dependencies,
  include_directories: _greeter_inc,
  cpp_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
    '-DNOCTALIA_GREETER_VERSION="' + meson.project_version() + '"',
    '-DNOCTALIA_GREETER_REVISION_HEADER="' + _git_revision_header_name + '"',
    '-DNOCTALIA_GREETER_LIBRARY_ASSETS_DIR="' + meson.project_source_root() / 'lib' / 'assets' + '"',
    '-DNOCTALIA_GREETER_ASSETS_DIR="' + meson.project_source_root() / 'assets' + '"',
    '-DNOCTALIA_GREETER_INSTALLED_ASSETS_DIR="' + get_option('datadir') / 'noctalia-greeter' / 'assets' + '"',
    '-DNOCTALIA_GREETER_INSTALLED_POLICY_PATH="' + get_option('prefix') / get_option('datadir') / 'polkit-1' / 'actions' / 'org.noctalia.greeter.apply-appearance.policy' + '"',
  ],
  install: true,
)

install_subdir('lib/assets',
  install_dir: get_option('datadir') / 'noctalia-greeter' / 'assets' / 'library',
  strip_directory: true,
)

install_subdir('assets',
  install_dir: get_option('datadir') / 'noctalia-greeter',
)

install_data(
  'scripts/noctalia-greeter-session',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  'scripts/noctalia-greeter-print-greetd-config',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  'scripts/noctalia-greeter-xsession',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  [
    'scripts/greetd_setup_lib.sh',
    'scripts/print_greetd_config.sh',
    'scripts/setup_greetd_pam.sh',
    'scripts/setup_greeter_system.sh',
  ],
  install_dir: get_option('datadir') / 'noctalia-greeter',
  install_mode: 'rwxr-xr-x',
)

_apply_appearance_support_sources = files(
  'src/greeter/logging.cpp',
  'src/greeter/appearance_sync.cpp',
  'src/greeter/greetd_user.cpp',
  'src/greeter/privileged_state_paths.cpp',
  'src/greeter/greeter_config_io.cpp',
    'src/greeter/greeter_layout.cpp',
  'src/greeter/greeter_preferences.cpp',
  'src/tools/secure_appearance_sync.cpp',
)

_apply_appearance_sources = _apply_appearance_support_sources + files('src/tools/apply_appearance_main.cpp')

_greeter_tools_inc = include_directories('src', 'lib')

apply_appearance_executable = executable('noctalia-greeter-apply-appearance',
  sources: _apply_appearance_sources,
  dependencies: [nlohmann_dep, tomlplusplus_dep, noctalia_ui_dep],
  include_directories: _greeter_tools_inc,
  cpp_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
  ],
  install: true,
)

installed_polkit_policy = configure_file(
  input: 'data/org.noctalia.greeter.apply-appearance.policy.in',
  output: 'org.noctalia.greeter.apply-appearance.policy',
  configuration: {
    'bindir': get_option('prefix') / get_option('bindir'),
  },
  install: true,
  install_dir: get_option('datadir') / 'polkit-1' / 'actions',
)

# Optional systemd/opentmpfiles drop-in (hardcoded greeter:greeter).
# Portable installs should run setup_greeter_system.sh in postinst instead.
install_data(
  'data/tmpfiles.d/noctalia-greeter.conf',
  install_dir: get_option('prefix') / 'lib' / 'tmpfiles.d',
)

noctalia_greeter_compositor_executable = executable('noctalia-greeter-compositor',
  sources: files(
    'src/compositor/noctalia_compositor.c',
    'src/compositor/output_layout_move.c',
    'src/compositor/output_render.c',
    'src/compositor/pointer_config.c',
    'src/compositor/system_keyboard_config.c',
    'src/config/output_identity.c',
    'src/greeter/logging.cpp',
    'src/greeter/greeter_config_io.cpp',
    'src/greeter/greeter_layout.cpp',
  ),
  dependencies: [
    wlroots_dep,
    libinput_dep,
    wayland_server_dep,
    xkbcommon_dep,
    tomlplusplus_dep,
  ],
  include_directories: include_directories('src', 'lib'),
  c_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion',
    '-DWLR_USE_UNSTABLE',
    '-DNOCTALIA_GREETER_INSTALLED_BINDIR="' + get_option('prefix') / get_option('bindir') + '"',
  ],
  install: true,
)

pointer_config_test = executable(
  'pointer-config-test',
  sources: files(
    'tests/pointer_config_test.cpp',
    'src/compositor/pointer_config.c',
    'src/greeter/greeter_config_io.cpp',
    'src/greeter/greeter_layout.cpp',
    'src/greeter/logging.cpp',
  ),
  dependencies: [libinput_dep, wlroots_dep, tomlplusplus_dep],
  include_directories: include_directories('src', 'lib'),
  c_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-DWLR_USE_UNSTABLE'],
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('pointer-config', pointer_config_test)

greetd_client_response_test = executable(
  'greetd-client-response-test',
  sources: files(
    'tests/greetd_client_response_test.cpp',
    'src/greetd/greetd_client.cpp',
    'src/greeter/logging.cpp',
  ),
  dependencies: [nlohmann_dep],
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('greetd-client-response', greetd_client_response_test)

passwordless_sync_policy_xml_test = executable(
  'passwordless-sync-policy-xml-test',
  sources: files(
    'tests/passwordless_sync_policy_xml_test.cpp',
    'src/tools/passwordless_sync_policy_xml.cpp',
  ),
  dependencies: [libxml2_dep],
  include_directories: include_directories('src', 'lib'),
  build_by_default: false,
)

test(
  'passwordless-sync-policy-xml',
  passwordless_sync_policy_xml_test,
  args: [
    installed_polkit_policy,
    get_option('prefix') / get_option('bindir') / 'noctalia-greeter-apply-appearance',
  ],
)

test(
  'passwordless-sync-help',
  noctalia_greeter_executable,
  args: ['passwordless-sync', '--help'],
)

test(
  'apply-appearance-compatibility',
  find_program('sh'),
  args: [
    meson.project_source_root() / 'tests/apply_appearance_compatibility_test.sh',
    apply_appearance_executable,
  ],
)

test(
  'session-wrapper',
  find_program('python3'),
  args: [
    meson.project_source_root() / 'tests/session_wrapper_test.py',
    meson.project_source_root() / 'scripts/noctalia-greeter-session',
  ],
  timeout: 60,
)

legacy_staging_compatibility_test = executable(
  'legacy-staging-compatibility-test',
  sources: _apply_appearance_support_sources + files(
    'src/greeter/appearance_config.cpp',
    'tests/legacy_staging_compatibility_test.cpp',
  ),
  dependencies: [nlohmann_dep, tomlplusplus_dep, noctalia_ui_dep],
  include_directories: _greeter_tools_inc,
  cpp_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
    '-DNOCTALIA_GREETER_TESTING',
  ],
  build_by_default: false,
)

test('legacy-staging-compatibility', legacy_staging_compatibility_test)

config_preservation_test = executable(
  'config-preservation-test',
  sources: files('tests/config_preservation_test.cpp', 'src/greeter/appearance_config.cpp'),
  objects: apply_appearance_executable.extract_objects(_apply_appearance_support_sources),
  dependencies: [nlohmann_dep, tomlplusplus_dep, noctalia_ui_dep],
  include_directories: _greeter_tools_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)
test('config-preservation', config_preservation_test)
test(
  'setup-config-preservation',
  find_program('sh'),
  args: [meson.project_source_root() / 'tests/setup_config_preservation_test.sh', config_preservation_test],
)

jxl_decoder_test = executable(
  'jxl-decoder-test',
  sources: files('tests/jxl_decoder_test.cpp', 'src/greeter/logging.cpp'),
  dependencies: [noctalia_ui_codecs_dep],
  include_directories: _greeter_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('jxl-decoder', jxl_decoder_test)

wallpaper_span_geometry_test = executable(
  'wallpaper-span-geometry-test',
  sources: files('tests/wallpaper_span_geometry_test.cpp'),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('wallpaper-span-geometry', wallpaper_span_geometry_test)

greeter_sessions_lookup_test = executable(
  'greeter-sessions-lookup-test',
  sources: files(
    'tests/greeter_sessions_lookup_test.cpp',
    'src/greeter/greeter_sessions.cpp',
  ),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('greeter-sessions-lookup', greeter_sessions_lookup_test)

system_keyboard_config_test = executable(
  'system-keyboard-config-test',
  sources: files(
    'tests/system_keyboard_config_test.c',
    'src/compositor/system_keyboard_config.c',
  ),
  include_directories: include_directories('src', 'lib'),
  c_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion'],
  build_by_default: false,
)

test('system-keyboard-config', system_keyboard_config_test)

output_layout_move_test = executable(
  'output-layout-move-test',
  sources: files(
    'tests/output_layout_move_test.c',
    'src/compositor/output_layout_move.c',
  ),
  dependencies: [wlroots_dep, wayland_server_dep, wayland_client_dep],
  include_directories: include_directories('src', 'lib'),
  c_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-DWLR_USE_UNSTABLE'],
  build_by_default: false,
)

test('output-layout-move', output_layout_move_test)

compositor_lifecycle_test = executable(
  'compositor-lifecycle-test',
  sources: files('tests/compositor_lifecycle_test.c'),
  objects: noctalia_greeter_compositor_executable.extract_objects(files(
    'src/compositor/output_layout_move.c',
    'src/compositor/output_render.c',
    'src/compositor/pointer_config.c',
    'src/compositor/system_keyboard_config.c',
    'src/config/output_identity.c',
    'src/greeter/logging.cpp',
    'src/greeter/greeter_config_io.cpp',
    'src/greeter/greeter_layout.cpp',
  )),
  dependencies: [wlroots_dep, libinput_dep, wayland_server_dep, xkbcommon_dep, tomlplusplus_dep],
  include_directories: include_directories('src', 'lib'),
  c_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-DWLR_USE_UNSTABLE'],
  link_args: ['-Wl,--wrap=wlr_backend_start', '-Wl,--wrap=wl_display_run'],
  build_by_default: false,
)

output_render_test = executable(
  'output-render-test',
  sources: files(
    'tests/output_render_test.c',
    'src/compositor/output_render.c',
  ),
  dependencies: [wlroots_dep, wayland_server_dep],
  include_directories: include_directories('src', 'lib'),
  c_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-DWLR_USE_UNSTABLE'],
  build_by_default: false,
)

test('output-render', output_render_test)

output_identity_test = executable(
  'output-identity-test',
  sources: files(
    'tests/output_identity_test.cpp',
    'src/config/output_identity.c',
  ),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('output-identity', output_identity_test)

test(
  'compositor-headless-layout',
  find_program('sh'),
  args: [
    meson.project_source_root() / 'tests/compositor_headless_test.sh',
    noctalia_greeter_compositor_executable,
    find_program('true').full_path(),
  ],
)

session_discovery_test = executable(
  'session-discovery-test',
  sources: files('tests/session_discovery_test.cpp', 'src/greeter/greeter_sessions.cpp'),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('session-discovery', session_discovery_test)

time_format_test = executable(
  'time-format-test',
  sources: files('tests/time_format_test.cpp', 'src/time/time_format.cpp'),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('time-format', time_format_test)

time_locale_test = executable(
  'time-locale-test',
  sources: files('tests/time_locale_test.cpp', 'src/time/time_locale.cpp', 'src/time/time_format.cpp'),
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('time-locale', time_locale_test)

greeter_frame_loop_test = executable(
  'greeter-frame-loop-test',
  sources: files('tests/greeter_frame_loop_test.cpp'),
  # Exercise the production window, rendering, and animation code without
  # recompiling it or linking the greeter's main().
  objects: noctalia_greeter_executable.extract_objects(_greeter_sources),
  dependencies: _greeter_dependencies,
  include_directories: _greeter_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  link_args: ['-Wl,--wrap=wl_proxy_add_listener', '-Wl,--wrap=eglSwapBuffers'],
  build_by_default: false,
)

greeter_layout_test = executable(
  'greeter-layout-test',
  sources: files('tests/greeter_layout_test.cpp'),
  objects: noctalia_greeter_executable.extract_objects(_greeter_sources),
  dependencies: _greeter_dependencies,
  include_directories: _greeter_inc,
  build_by_default: false,
)
test('greeter-layout', greeter_layout_test)

greeter_auth_controller_test = executable(
  'greeter-auth-controller-test',
  sources: files('tests/greeter_auth_controller_test.cpp'),
  objects: noctalia_greeter_executable.extract_objects(_greeter_sources),
  dependencies: _greeter_dependencies,
  include_directories: _greeter_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test('greeter-auth-controller', greeter_auth_controller_test)

test(
  'greeter-frame-loop',
  find_program('sh'),
  args: [
    meson.project_source_root() / 'tests/greeter_frame_loop_test.sh',
    noctalia_greeter_compositor_executable,
    greeter_frame_loop_test,
  ],
  timeout: 30,
)

greeter_user_switch_test = executable(
  'greeter-user-switch-test',
  sources: files('tests/greeter_user_switch_test.cpp'),
  objects: noctalia_greeter_executable.extract_objects(_greeter_sources),
  dependencies: _greeter_dependencies + [dependency('threads')],
  include_directories: _greeter_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  link_args: ['-Wl,--wrap=wl_proxy_add_listener', '-Wl,--wrap=poll', '-Wl,--wrap=__poll_chk'],
  build_by_default: false,
)

test(
  'greeter-user-switch',
  find_program('sh'),
  args: [
    meson.project_source_root() / 'tests/greeter_user_switch_test.sh',
    noctalia_greeter_compositor_executable,
    greeter_user_switch_test,
  ],
  timeout: 60,
)

test(
  'compositor-lifecycle',
  find_program('sh'),
  args: [
    meson.project_source_root() / 'tests/compositor_lifecycle_test.sh',
    compositor_lifecycle_test,
    greeter_user_switch_test,
  ],
  timeout: 60,
)

greeter_login_flow_test = executable(
  'greeter-login-flow-test',
  sources: files('tests/greeter_login_flow_test.cpp'),
  objects: noctalia_greeter_executable.extract_objects(_greeter_sources),
  dependencies: _greeter_dependencies,
  include_directories: _greeter_inc,
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test(
  'greeter-login-flow',
  find_program('python3'),
  args: [
    meson.project_source_root() / 'tests/greeter_login_flow_test.py',
    noctalia_greeter_compositor_executable,
    greeter_login_flow_test,
  ],
  timeout: 60,
)

wallpaper_program_pixel_test = executable(
  'wallpaper-program-pixel-test',
  sources: files(
    'tests/wallpaper_program_pixel_test.cpp',
    'src/greeter/render/wallpaper_program.cpp',
    'src/greeter/render/wallpaper_node.cpp',
  ),
  dependencies: [noctalia_ui_dep],
  include_directories: include_directories('src', 'lib'),
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow'],
  build_by_default: false,
)

test(
  'wallpaper-program-pixels',
  wallpaper_program_pixel_test,
  env: ['LIBGL_ALWAYS_SOFTWARE=1'],
)


test(
  'version-metadata',
  find_program('python3'),
  args: [
    meson.project_source_root() / 'tests/version_metadata_test.py',
    noctalia_greeter_executable,
    meson.project_source_root() / 'VERSION',
    _git_revision_h,
    meson.project_source_root() / 'scripts/validate-release-version',
  ],
)
