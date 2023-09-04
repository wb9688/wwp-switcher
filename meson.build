project(
    'wwp-switcher',
    'c',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: [
        'cpp_std=c++17',
        'c_std=c11',
        'warning_level=2',
        'werror=false',
    ],
)

wayfire = dependency('wayfire')
giomm = dependency('giomm-2.4')
wlroots = dependency('wlroots')
wfconfig = dependency('wf-config')
cairo = dependency('cairo-gobject')
gdk = dependency('gdk-3.0')
gdkpixbuf = dependency('gdk-pixbuf-2.0')
pixman = dependency('pixman-1')
pangocairo = dependency('pangocairo')
gio = dependency('gio-unix-2.0')

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic'], language:'cpp')

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')

wwp_switcher = shared_module('wwp-switcher', 'wwp-switcher.cpp',
    dependencies: [wayfire, wlroots, wfconfig, cairo, giomm, gdk, gdkpixbuf],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_data('wwp-switcher.xml', install_dir: wayfire.get_variable(pkgconfig: 'metadatadir'))
