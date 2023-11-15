project('ac01-nvparm', 'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0',
)

add_project_arguments('-Wno-implicit-fallthrough', language: 'c')

executable('nvparm',
           'nvparm.c',
           implicit_include_directories: false,
           install: true,
           install_dir: get_option('sbindir'))
