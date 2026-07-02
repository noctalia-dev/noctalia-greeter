project('noctalia-greeter', ['c', 'cpp'],
  version: '1.0.0',
  default_options: [
    'cpp_std=c++20',
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
    '-march=native', '-mtune=native',
    '-fomit-frame-pointer', '-ffunction-sections', '-fdata-sections',
    language: ['c', 'cpp'],
  )
  add_project_link_arguments(
    '-Wl,--gc-sections', '-Wl,--as-needed',
    language: ['c', 'cpp'],
  )
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
glib_dep           = dependency('glib-2.0')
gobject_dep        = dependency('gobject-2.0')
gio_dep            = dependency('gio-2.0')
wayland_server_dep = dependency('wayland-server')
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

stb_dep = declare_dependency(
  include_directories: include_directories('third_party/stb', is_system: true),
)

libwebp_dep = dependency('libwebp', required: true)

nlohmann_dep = declare_dependency(
  include_directories: include_directories('third_party/nlohmann', is_system: true),
)

# ── Vendored: tomlplusplus (header-only) ─────────────────────────────────
if get_option('system_tomlplusplus')
  tomlplusplus_dep = dependency('tomlplusplus')
else
  tomlplusplus_dep = declare_dependency(
    include_directories: include_directories('third_party/tomlplusplus', is_system: true),
  )
endif

# ── Wayland protocol generation ───────────────────────────────────────────────
wayland_scanner    = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos_dep.get_variable('pkgdatadir')

_protocol_sources = []

foreach _p : [
  ['xdg-shell', wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml'],
  ['cursor-shape-v1', 'protocols/cursor-shape-v1.xml'],
  ['fractional-scale-v1', wayland_protos_dir / 'staging/fractional-scale/fractional-scale-v1.xml'],
  ['viewporter', wayland_protos_dir / 'stable/viewporter/viewporter.xml'],
]
  _name = _p[0]
  _xml  = _p[1]

  _h = custom_target(_name + '-client-header',
    input:   _xml,
    output:  _name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  _c = custom_target(_name + '-client-source',
    input:   _xml,
    output:  _name + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: _h,
  )
  _protocol_sources += _c
endforeach

_greeter_sources = files(
  'src/core/log.cpp',
  'src/core/deferred_call.cpp',
  'src/core/resource_paths.cpp',
  'src/core/ui_phase.cpp',
  'src/wayland/wayland_client.cpp',
  'src/wayland/wayland_seat.cpp',
  'src/render/gl_shared_context.cpp',
  'src/render/render_context.cpp',
  'src/render/render_target.cpp',
  'src/render/backend/render_backend.cpp',
  'src/render/backend/gles_render_backend.cpp',
  'src/render/backend/gles_framebuffer.cpp',
  'src/render/backend/gles_texture_manager.cpp',
  'src/render/core/color.cpp',
  'src/render/core/shader_program.cpp',
  'src/render/core/image_file_loader.cpp',
  'src/render/core/image_decoder.cpp',
  'src/render/core/async_texture_cache.cpp',
  'src/render/core/shared_texture_cache.cpp',
  'src/render/core/blur_cache.cpp',
  'src/render/core/cached_layer.cpp',
  'src/render/programs/rect_program.cpp',
  'src/render/programs/image_program.cpp',
  'src/render/programs/glyph_program.cpp',
  'src/render/programs/linear_gradient_program.cpp',
  'src/render/programs/blur_program.cpp',
  'src/render/programs/wallpaper_program.cpp',
  'src/render/programs/spinner_program.cpp',
  'src/render/programs/screen_corner_program.cpp',
  'src/render/programs/effect_program.cpp',
  'src/render/programs/audio_spectrum_program.cpp',
  'src/render/programs/graph_program.cpp',
  'src/render/scene/node.cpp',
  'src/render/scene/input_area.cpp',
  'src/render/scene/input_dispatcher.cpp',
  'src/render/scene/graph_node.cpp',
  'src/render/text/cairo_text_renderer.cpp',
  'src/render/text/cairo_glyph_renderer.cpp',
  'src/render/text/glyph_registry.cpp',
  'src/render/animation/animation.cpp',
  'src/render/animation/animation_manager.cpp',
  'src/render/animation/motion_service.cpp',
  'src/ui/controls/box.cpp',
  'src/ui/controls/button.cpp',
  'src/ui/controls/input.cpp',
  'src/ui/controls/search_field.cpp',
  'src/ui/controls/label.cpp',
  'src/ui/controls/flex.cpp',
  'src/ui/controls/glyph.cpp',
  'src/ui/controls/image.cpp',
  'src/ui/controls/scroll_view.cpp',
  'src/ui/controls/spacer.cpp',
  'src/ui/controls/spinner.cpp',
  'src/ui/controls/separator.cpp',
  'src/ui/palette.cpp',
  'src/theme/builtin_palettes.cpp',
  'src/ui/style.cpp',
  'src/accounts/accounts_icon.cpp',
  'src/greetd/greetd_client.cpp',
  'src/greeter/appearance_sync.cpp',
  'src/greeter/appearance_config.cpp',
  'src/greeter/session_config.cpp',
  'src/greeter/greeter_config_io.cpp',
  'src/greeter/greeter_preferences.cpp',
  'src/greeter/greeter_sessions.cpp',
  'src/greeter/greeter_surface.cpp',
  'src/greeter/greeter_window.cpp',
  'src/greeter/logind_resume.cpp',
  'src/greeter/power_actions.cpp',
  'src/greeter/greeter.cpp',
  'src/main.cpp',
) + _protocol_sources

_greeter_inc = [
  include_directories('src'),
  include_directories('third_party/stb', is_system: true),
  include_directories('third_party/nlohmann', is_system: true),
  include_directories('third_party/tomlplusplus', is_system: true),
  include_directories('third_party/wuffs', is_system: true),
]

executable('noctalia-greeter',
  sources: _greeter_sources,
  dependencies: [
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
    glib_dep,
    gobject_dep,
    gio_dep,
    stb_dep,
    libwebp_dep,
    nlohmann_dep,
    tomlplusplus_dep,
  ],
  include_directories: _greeter_inc,
  cpp_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
    '-DNOCTALIA_GREETER_VERSION="' + meson.project_version() + '"',
    '-DNOCTALIA_GREETER_ASSETS_DIR="' + meson.project_source_root() / 'assets' + '"',
    '-DNOCTALIA_GREETER_INSTALLED_ASSETS_DIR="' + get_option('datadir') / 'noctalia-greeter' / 'assets' + '"',
  ],
  install: true,
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
  [
    'scripts/greetd_setup_lib.sh',
    'scripts/print_greetd_config.sh',
    'scripts/setup_greetd_pam.sh',
    'scripts/setup_greeter_system.sh',
  ],
  install_dir: get_option('datadir') / 'noctalia-greeter',
  install_mode: 'rwxr-xr-x',
)

_apply_appearance_sources = files(
  'src/core/log.cpp',
  'src/greeter/appearance_sync.cpp',
  'src/greeter/greetd_user.cpp',
  'src/greeter/greeter_config_io.cpp',
  'src/greeter/greeter_preferences.cpp',
  'src/tools/apply_appearance_main.cpp',
)

_greeter_tools_inc = include_directories('src')

executable('noctalia-greeter-apply-appearance',
  sources: _apply_appearance_sources,
  dependencies: [nlohmann_dep, tomlplusplus_dep],
  include_directories: _greeter_tools_inc,
  cpp_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
  ],
  install: true,
)

install_data(
  'data/org.noctalia.greeter.apply-appearance.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions',
)

executable('noctalia-greeter-compositor',
  sources: files(
    'src/compositor/noctalia_compositor.c',
    'src/core/log.cpp',
    'src/greeter/greeter_config_io.cpp',
  ),
  dependencies: [
    wlroots_dep,
    libinput_dep,
    wayland_server_dep,
    xkbcommon_dep,
    tomlplusplus_dep,
  ],
  include_directories: include_directories('src'),
  c_args: [
    '-Wall', '-Wextra', '-Wpedantic', '-Wconversion',
    '-DWLR_USE_UNSTABLE',
    '-DNOCTALIA_GREETER_INSTALLED_BINDIR="' + get_option('prefix') / get_option('bindir') + '"',
  ],
  install: true,
)
