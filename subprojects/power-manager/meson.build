project(
    'power-manager',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20',
    ],
)

cpp = meson.get_compiler('cpp')

executable(
    'power-manager',
    [
        'power_manager_main.cpp',
        'src/power_limit_interface.cpp',
        'src/power_manager.cpp',
    ],
    dependencies: [
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        dependency('phosphor-logging'),
        dependency('phosphor-dbus-interfaces'),
    ],
    include_directories: ['include'],
    install: true,
    install_dir: get_option('sbindir')
)

uint_files = [
    'xyz.openbmc_project.power.manager.service',
    'power-limit-oem@.service',
]

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('sbindir', get_option('prefix') / get_option('sbindir'))

foreach file : uint_files
    configure_file(
        input: file,
        output: file,
        configuration: conf_data,
        install: true,
        install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
    )
endforeach

share_power_manager_folder = get_option('datadir') / 'power-manager'
install_emptydir(share_power_manager_folder)
conf_files = [
    'power-manager-cfg.json',
    ]

foreach file : conf_files
    install_data(
        file,
        install_mode: 'rwxr-xr-x',
        install_dir: share_power_manager_folder
    )
endforeach

script_files = [
    'script/power_limit_oem.sh',
]

foreach file : script_files
    install_data(
        file,
        install_mode: 'rwxr-xr-x',
        install_dir: get_option('sbindir')
    )
endforeach