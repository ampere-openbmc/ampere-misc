project(
    'crash-capture-manager',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++23',
    ],
)

cpp = meson.get_compiler('cpp')

# Include libspinorfs as a static library, compiled with `make`.
libspinorfs_dir = meson.current_source_dir() + '../recipe-sysroot/usr/lib'
libspinorfs_dep = cpp.find_library('libspinorfs', dirs : libspinorfs_dir)

executable(
    'crash-capture-manager',
    [
        'crash_capture_main.cpp',
        'crash_capture_interface.cpp',
        'bert_handler.cpp',
        'utils.cpp',
    ],
    dependencies: [
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        dependency('phosphor-dbus-interfaces'),
        libspinorfs_dep,
    ],
    install: true,
    install_dir: get_option('bindir')
)

uint_files = [
    'com.ampere.crashcapture.manager.service'
]

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('BERT_HOSTON_TIMEOUT', get_option('ampere-bert-hoston-timer'))
conf_data.set('BERT_HOSTFAIL_TIMEOUT', get_option('ampere-bert-hostfail-timer'))
conf_data.set('BERT_CLAIMSPI_TIMEOUT', get_option('ampere-bert-claim-spi-timer'))
conf_data.set_quoted('HANDSHAKE_SPI_SCRIPT', get_option('ampere-handshake-spi-script'))
conf_data.set_quoted('CRASHDUMP_LOG_PATH', get_option('crashdump-log-path'))
conf_data.set_quoted('POWER_CONTROL_LOCK_SCRIPT', get_option('ampere-power-control-lock-script'))
conf_data.set('BERT_POWER_LOCK_TIMEOUT', get_option('ampere-bert-powerlock-timer'))
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))

configure_file(output: 'config.h',
  configuration: conf_data
)

foreach file : uint_files
    configure_file(
        input: file,
        output: file,
        configuration: conf_data,
        install: true,
        install_dir: systemd.get_variable('systemdsystemunitdir')
    )
endforeach
