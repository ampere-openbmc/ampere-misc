project(
    'nmi-manager',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20',
    ],
)

cpp = meson.get_compiler('cpp')

executable(
    'nmi-manager',
    [
        'nmi_interface.cpp',
        'nmi.cpp',
    ],
    dependencies: [
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('phosphor-dbus-interfaces'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

uint_files = [
    'xyz.openbmc_project.nmi.manager.service',
    'nmi.service'
]

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))

foreach file : uint_files
    configure_file(
        input: file,
        output: file,
        configuration: conf_data,
        install: true,
        install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
    )
endforeach
