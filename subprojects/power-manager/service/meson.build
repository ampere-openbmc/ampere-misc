uint_files = [
    'xyz.openbmc_project.power.manager.service',
    'power-cap-action-oem.service',
    'power-cap-exceeds-limit.service',
    'power-cap-drops-below-limit.service',
]

foreach file : uint_files
    configure_file(
        input: file,
        output: file,
        configuration: conf_data,
        install: true,
        install_dir: systemd.get_variable('systemdsystemunitdir')
    )
endforeach
