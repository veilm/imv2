project(
  'imv',
  ['c'],
  version: '5.0.1',
  license: 'MIT',
  meson_version: '>= 0.62',
  default_options: ['c_std=c99', 'warning_level=3'],
)

version = '@0@'.format(meson.project_version())
prog_git = find_program('git', required: false)
if prog_git.found()
  git_description = run_command([prog_git, 'describe', '--dirty', '--always', '--tags'], check: false)
  if git_description.returncode() == 0
    version = git_description.stdout().strip()
  endif
endif
add_project_arguments('-DIMV_VERSION="@0@"'.format(version), language: 'c')

add_project_arguments('-D_XOPEN_SOURCE=700', language: 'c')

add_project_arguments('-DRSVG_DISABLE_DEPRECATION_WARNINGS', language: 'c')

# TODO: Remove once cmocka 2 gets picked up by all builders
add_project_arguments('-DCMOCKA_DISABLE_DEPRECATION_WARNINGS', language: 'c')

cc = meson.get_compiler('c')
dep_null = dependency('', required: false)
m_dep = cc.find_library('m', required : false)

_windows = get_option('windows')
if _windows == 'wayland'
  build_wayland = true
  build_x11 = false
  target_single_ws = true
elif _windows == 'x11'
  build_wayland = false
  build_x11 = true
  target_single_ws = true
else
  build_wayland = true
  build_x11 = true
  target_single_ws = false
endif

_unicode = get_option('unicode')
if _unicode == 'icu'
  unicode_lib = dependency('icu-uc')
  add_project_arguments('-DIMV_USE_ICU', language: 'c')
elif _unicode == 'grapheme'
  unicode_lib = cc.find_library('grapheme')
  add_project_arguments('-DIMV_USE_GRAPHEME', language: 'c')
endif

gl_dep = dependency('gl', required: false)
if not gl_dep.found()
  # libglvnd fallback for pure-wayland systems
  gl_dep = dependency('opengl')
endif

deps_for_imv = [
  dependency('pangocairo'),
  gl_dep,
  dependency('threads'),
  dependency('xkbcommon'),
  unicode_lib,
  dependency('inih', fallback : ['inih', 'inih_dep']),
  m_dep,
]

files_common = files(
  'src/backends.c',
  'src/binds.c',
  'src/bitmap.c',
  'src/canvas.c',
  'src/commands.c',
  'src/console.c',
  'src/image.c',
  'src/imv.c',
  'src/ipc.c',
  'src/ipc_common.c',
  'src/keyboard.c',
  'src/list.c',
  'src/log.c',
  'src/navigator.c',
  'src/source.c',
  'src/thumbs.c',
  'src/viewport.c',
)

enabled_window_systems = []

targets_wayland = []
deps_for_wayland = []
if build_wayland
  files_wayland = files('src/wl_window.c', 'src/platform_posix.c')
  deps_for_wayland = [
    dependency('wayland-client'),
    dependency('wayland-cursor'),
    dependency('wayland-egl'),
    dependency('egl'),
    cc.find_library('rt'),
  ]

  wl_protos = dependency('wayland-protocols')
  wl_scanner = find_program('wayland-scanner', native: true)
  proto_dir = wl_protos.get_variable('pkgdatadir')

  scan_h = generator(wl_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@'])

  scan_c = generator(wl_scanner,
    output: '@BASENAME@-protocol.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@'])

  protocols = [
    proto_dir / 'stable/xdg-shell/xdg-shell.xml',
    proto_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
    proto_dir / 'unstable/pointer-gestures/pointer-gestures-unstable-v1.xml',
  ]

  targets_wayland = [scan_h.process(protocols), scan_c.process(protocols)]

  enabled_window_systems += 'wayland'
endif

targets_x11 = []
deps_for_x11 = []
if build_x11
  files_x11 = files('src/x11_window.c', 'src/platform_posix.c')
  deps_for_x11 = [
    dependency('x11'),
    dependency('gl'),
    dependency('xcb'),
    dependency('xkbcommon-x11'),
  ]
  enabled_window_systems += 'x11'
endif

files_msg = files('src/imv_msg.c', 'src/ipc_common.c')

enabled_backends = []
supported_mime_types = []
foreach backend : [
  ['farbfeld', 'builtin', [], [], ['image/x-farbfeld']],
  ['libtiff', 'dependency', 'libtiff-4', [], ['image/tiff', 'image/tiff-fx']],
  ['libpng', 'dependency', 'libpng', [], ['image/png', 'image/x-png']],
  ['libjpeg', 'dependency', 'libturbojpeg', [], ['image/jpeg', 'image/jpg', 'image/pjpeg']],
  ['librsvg', 'dependency', 'librsvg-2.0', '>= 2.44', ['image/svg+xml']],
  ['libnsgif', 'dependency', 'libnsgif', '1.0.0', ['image/gif']],
  ['libnsbmp', 'dependency', 'libnsbmp', [], ['image/bmp', 'image/x-bmp']],
  ['libheif', 'dependency', 'libheif', '>= 1.13.0', ['image/heif', 'image/avif']],
  ['libjxl', 'dependency', 'libjxl', [], ['image/jxl']],
  ['libwebp', 'dependency', 'libwebpdemux', [], ['image/webp']],
  ['qoi', 'header', 'qoi.h', [], ['image/qoi']],
]
  _backend_name = backend[0]
  _dep_type = backend[1]
  _dep_name = backend[2]
  _mime_types = backend[4]

  if _dep_type == 'dependency'
    _dep = dependency(_dep_name, required: get_option(_backend_name), version: backend[3])
  elif _dep_type == 'library'
    _dep = cc.find_library(_dep_name, required: get_option(_backend_name))
  elif _dep_type == 'header'
    _dep = (cc.has_header(_dep_name, required: get_option(_backend_name))
      ? declare_dependency()
      : disabler())
  elif _dep_type == 'builtin'
    _dep = (get_option(_backend_name).allowed()
      ? declare_dependency()
      : disabler())
  else
    error('invalid dep type: @0@'.format(_dep_type))
  endif

  if _dep.found()
    deps_for_imv += _dep
    files_common += files('src/backend_@0@.c'.format(_backend_name))
    add_project_arguments('-DIMV_BACKEND_@0@'.format(_backend_name.to_upper()), language: 'c')
    enabled_backends += _backend_name
    supported_mime_types += _mime_types
  endif
endforeach

lcms2 = dependency('lcms2', required: get_option('lcms2'))
libjpeg = dependency('libjpeg', required: get_option('lcms2'))
if lcms2.found() and libjpeg.found()
  deps_for_imv += lcms2
  deps_for_imv += libjpeg
  add_project_arguments('-DIMV_BACKEND_LCMS2', language: 'c')
endif

libimv = static_library(
  'imv',
  files_common,
  dependencies: deps_for_imv,
)

executable(
  'imv-msg',
  files('src/imv_msg.c', 'src/ipc_common.c'),
  dependencies: deps_for_imv,
  install: true,
  install_dir: get_option('bindir'),
)

foreach ws : ['wayland', 'x11']
  if get_variable('build_' + ws)
    executable(
      target_single_ws ? 'imv' : 'imv-@0@'.format(ws),
      [get_variable('files_' + ws), files('src/main.c')],
      get_variable('targets_' + ws),
      link_with: libimv,
      dependencies: [deps_for_imv, get_variable('deps_for_' + ws)],
      install: true,
      install_dir: get_option('bindir'),
    )
  endif
endforeach

if get_option('contrib-commands')
  install_data(
    files('contrib/imv-dir'),
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x',
  )
endif

if not target_single_ws
  configure_file(
    input: 'files/imv.in',
    output: 'imv',
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x',
    configuration: {
      'bindir': get_option('prefix') / get_option('bindir')
    }
  )
endif

desktop_list = [
  'imv',
]
if get_option('contrib-commands')
  desktop_list += [
    'imv-dir',
  ]
endif
foreach desktop: desktop_list
  configure_file(
    input: 'files/@0@.desktop.in'.format(desktop),
    output: '@0@.desktop'.format(desktop),
    install_dir: '@0@/applications'.format(get_option('datadir')),
    install_mode: 'rw-r--r--',
    configuration: {
      'supported_mime_types': ';'.join(supported_mime_types)
    }
  )
endforeach

install_data(
  files('files/imv_config'),
  install_dir: get_option('sysconfdir'),
  install_mode: 'rw-r--r--',
)

dep_cmocka = dependency('cmocka', required: get_option('test'))
xxd = find_program('xxd', required: get_option('test'))

if dep_cmocka.found() and xxd.found()
  embed = generator(
    xxd,
    output: '@PLAINNAME@.c',
    arguments: ['-n', '@PLAINNAME@', '-i', '@INPUT@', '@OUTPUT@']
  )

  test_images = static_library(
    'test_images',
    embed.process([
      'test/data/sample.tiff',
      'test/data/sample.png',
      'test/data/sample.webp',
      'test/data/sample.jpeg',
      'test/data/sample_cmyk.jpeg',
    ])
  )

  foreach test : ['list', 'navigator', 'backends']
    test(
      'test_@0@'.format(test),
      executable(
        'test_@0@'.format(test),
        [files('test/@0@.c'.format(test), 'src/dummy_window.c', 'src/platform_dummy.c')],
        link_with: [libimv, test_images],
        include_directories: include_directories('src'),
        dependencies: [deps_for_imv, dep_cmocka],
      )
    )
  endforeach
endif

prog_a2x = find_program('a2x', required: get_option('man'))

if prog_a2x.found()
  man_list = [
    [1, 'imv'],
    [1, 'imv-msg'],
    [5, 'imv'],
  ]
  if get_option('contrib-commands')
    man_list += [
      [1, 'imv-dir'],
    ]
  endif

  foreach man : man_list
    _section = man[0]
    _topic = man[1]
    custom_target(
      '@0@(@1@)'.format(_topic, _section),
      input: 'doc/@0@.@1@.txt'.format(_topic, _section),
      output: '@0@.@1@'.format(_topic, _section),
      command: [
        prog_a2x,
        '--no-xmllint',
        '--doctype', 'manpage',
        '--format', 'manpage',
        '--destination-dir', meson.current_build_dir(),
        '@INPUT@'
      ],
      install: true,
      install_dir: '@0@/man@1@'.format(get_option('mandir'), _section)
    )
  endforeach
endif

message('\n\n'
+ 'Building imv @0@\n\n'.format(meson.project_version())
+ 'Window systems enabled:\n- ' + '\n- '.join(enabled_window_systems) + '\n\n'
+ 'Backends enabled:\n- ' + '\n- '.join(enabled_backends) + '\n'
)
