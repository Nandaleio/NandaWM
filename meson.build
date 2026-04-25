project('nandawm', 'c',
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'c_std=c11',
  ])

wlroots       = dependency('wlroots-0.18')
wayland_server = dependency('wayland-server')
xkbcommon     = dependency('xkbcommon')

executable('nandawm',
  sources: [
    'src/main.c',
    'src/output.c',
    'src/input.c',
    'src/view.c',
    'src/layer.c',
  ],
  include_directories: include_directories('src'),
  dependencies: [wlroots, wayland_server, xkbcommon],
  install: true,
)
