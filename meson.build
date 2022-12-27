project(
    'amere-misc',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20'
    ]
)

cpp = meson.get_compiler('cpp')

build_nmi_manager = get_option('nmi-manager')
if build_nmi_manager.enabled()
    subproject('nmi-manager')
endif

build_cpld_fwupdate = get_option('ampere-cpld-fwupdate')
if build_cpld_fwupdate.enabled()
    subproject('ampere-cpld-fwupdate')
endif

build_state_logger = get_option('state-logger')
if build_state_logger.enabled()
    subproject('state-logger')
endif

build_mctp_ctrl = get_option('mctp-ctrl')
if build_mctp_ctrl.enabled()
    subproject('mctp-ctrl', 
               default_options: [
		 'delay-before-add-terminus=' + get_option('mctp-delay-before-add-terminus').to_string(),
		 'delay-before-add-second-terminus=' + get_option('mctp-delay-before-add-second-terminus').to_string(),
		 'checking-s1-ready-time-out=' + get_option('mctp-checking-s1-ready-time-out').to_string()
	       ]
    )
endif

build_ampere_trst_wa = get_option('ampere-trst-wa')
if build_ampere_trst_wa.enabled()
    subproject('ampere-trst-wa')
endif

build_power_manager = get_option('power-manager')
if build_power_manager.enabled()
    subproject('power-manager')
endif
