project('ampere-cpld-fwupdate', 'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0',
)

add_project_arguments('-Wno-psabi', language: 'c')

deps = [dependency('systemd'),
]

executable('ampere_cpldupdate_jtag',
           'src/cpldupdate-jtag.c',
           'src/ast-jtag.c',
           'src/ast-jtag-intf.c',
           'src/i2c-lib.c',
           'src/lattice.c',
           'src/anlogic.c',
           'src/cpld.c',
           implicit_include_directories: false,
           include_directories: ['include'],
           dependencies: deps,
           install: true,
           install_dir: get_option('bindir'))
           
executable('ampere_cpldupdate_i2c',
           'src/cpldupdate-i2c.c',
           'src/ast-jtag.c',
           'src/ast-jtag-intf.c',
           'src/i2c-lib.c',
           'src/lattice.c',
           'src/anlogic.c',
           'src/cpld.c',
           implicit_include_directories: false,
           include_directories: ['include'],
           dependencies: deps,
           install: true,
           install_dir: get_option('bindir'))
