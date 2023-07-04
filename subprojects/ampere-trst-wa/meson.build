project('ampere-trst-wa', 'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0',
)

add_project_arguments('-Wno-psabi', language: 'c')
systemd = dependency('systemd')

executable('ampere_trst_wa',
           'ampere-trst-wa.c',
           implicit_include_directories: false,
           dependencies: [
                dependency('systemd'),
                dependency('libgpiod')
                ],
           install: true,
           install_dir: get_option('sbindir'))

configure_file(
    copy: true,
    input: 'ampere-trst-wa.service.in',
    output: 'ampere-trst-wa.service',
    install: true,
    install_dir: systemd.get_variable('systemdsystemunitdir')
)
