project(
    'mctp-ctrl',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20',
    ],
)

cpp = meson.get_compiler('cpp')

conf_data = configuration_data()
conf_data.set('DELAY_BEFORE_ADD_TERMINUS', get_option('delay-before-add-terminus'))
conf_data.set('DELAY_BEFORE_ADD_SECOND_TERMINUS', get_option('delay-before-add-second-terminus'))
conf_data.set('CHECKING_S1_READY_TIME_OUT', get_option('checking-s1-ready-time-out'))
conf_data.set('sbindir', get_option('prefix') / get_option('sbindir'))

systemd = dependency('systemd')
configure_file(output: 'config.h',
  configuration: conf_data
)

executable(
    'ampere-mctp-ctrl',
    'mctpCtrl.cpp',
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
    input: 'xyz.openbmc_project.AmpereMctpCtrl.service',
    output: 'xyz.openbmc_project.AmpereMctpCtrl.service',
    install: true,
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
)
