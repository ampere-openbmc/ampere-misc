project('flash-utils', 'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0',
)

add_project_arguments(['-Wno-unused-parameter',
                       '-Wno-unused-result',
                       '-Wno-sign-compare'
                      ],
                     language: 'c')

executable('ampere_eeprom_prog',
           'ampere_eeprom_prog.c',
           implicit_include_directories: false,
            dependencies: [
                dependency('zlib')
            ],
           install: true,
           install_dir: get_option('sbindir'))

executable('ampere_fru_upgrade',
           'ampere_fru_upgrade.c',
           implicit_include_directories: false,
           dependencies: [
                dependency('zlib')
            ],
           install: true,
           install_dir: get_option('sbindir'))
