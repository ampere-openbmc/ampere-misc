project(
    'ampere-state-logger',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++23',
    ],
)

cpp = meson.get_compiler('cpp')

executable(
    'ampere-state-logger', 'ampere-state-logger.cpp',
    dependencies: [
        dependency('systemd'),
        dependency('sdbusplus'),
        dependency('threads'),
        dependency('phosphor-logging'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

systemd = dependency('systemd')

fs = import('fs')
fs.copyfile(
  'xyz.openbmc_project.state_logger.service',
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir')
  )
