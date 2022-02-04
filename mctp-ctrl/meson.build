project(
    'mctp-ctrl',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

cpp = meson.get_compiler('cpp')

conf_data = configuration_data()
conf_data.set('DELAY_BEFORE_ADD_TERMINUS', get_option('delay-before-add-terminus'))
conf_data.set('sbindir', get_option('prefix') / get_option('sbindir'))

systemd = dependency('systemd')
configure_file(output: 'config.h',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
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

