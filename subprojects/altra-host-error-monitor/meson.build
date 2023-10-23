project(
    'altra-host-error-monitor',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++23',
    ],
)

cpp = meson.get_compiler('cpp')

deps = [dependency('libsystemd', version : '>=221'),
        dependency('systemd'),
        dependency('sdbusplus'),
        dependency('threads'),
        ]

executable(
        'altra-host-error-monitor',
        'altra-host-error-monitor.cpp',
        dependencies: deps,
        install: true,
        include_directories : ['include'],
        install_dir: get_option('sbindir')
        )

systemd = dependency('systemd')
configure_file(
    copy: true,
    input: 'xyz.openbmc_project.altra_host_error_monitor.service',
    output: 'xyz.openbmc_project.altra_host_error_monitor.service',
    install: true,
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
)

share_altra_host_error_monitor = get_option('datadir') / 'altra-host-error-monitor'
install_emptydir(share_altra_host_error_monitor)
conf_files = [
    'config.json',
    ]

foreach file : conf_files
    install_data(
        file,
        install_mode: 'rwxr-xr-x',
        install_dir: share_altra_host_error_monitor
    )
endforeach
