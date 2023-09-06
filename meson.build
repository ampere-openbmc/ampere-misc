project(
    'amere-misc',
    'cpp',
    version: '1.0',
    meson_version: '>=1.1.1',
    default_options: [
        'cpp_std=c++23'
    ]
)

cpp = meson.get_compiler('cpp')

if get_option('nmi-manager').enabled()
    subproject('nmi-manager')
endif

if get_option('ampere-cpld-fwupdate').enabled()
    subproject('ampere-cpld-fwupdate')
endif

if get_option('state-logger').enabled()
    subproject('state-logger')
endif

if get_option('mctp-ctrl').enabled()
    subproject('mctp-ctrl', 
               default_options: [
		 'delay-before-add-terminus=' + get_option('mctp-delay-before-add-terminus').to_string(),
		 'delay-before-add-second-terminus=' + get_option('mctp-delay-before-add-second-terminus').to_string(),
		 'checking-s1-ready-time-out=' + get_option('mctp-checking-s1-ready-time-out').to_string()
	       ]
    )
endif

if get_option('ampere-trst-wa').enabled()
    subproject('ampere-trst-wa')
endif

if get_option('power-manager').enabled()
    subproject('power-manager')
endif

if get_option('crash-capture-manager').enabled()
    subproject('crash-capture-manager')
endif
