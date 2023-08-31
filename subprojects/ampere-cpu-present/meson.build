project(
    'ampere-cpu-present',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++23',
    ],
)

cpp = meson.get_compiler('cpp')

conf_data = configuration_data()

systemd = dependency('systemd')
configure_file(output: 'config.h',
  configuration: conf_data
)

executable(
    'ampere-cpu-present',
    'ampereCpuPresent.cpp',
    dependencies: [
        dependency('systemd'),
        dependency('sdbusplus'),
        dependency('gpioplus'),
    ],
    install: true,
    install_dir: get_option('sbindir')
)

configure_file(
    copy: true,
    input: 'xyz.openbmc_project.Ampere.CpuPresent.service',
    output: 'xyz.openbmc_project.Ampere.CpuPresent.service',
    install: true,
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
)
