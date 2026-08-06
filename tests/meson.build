gtest_dep = dependency('gtest', main: false, disabler: true, required: false)
gmock_dep = dependency('gmock', disabler: true, required: false)
if not gtest_dep.found() or not gmock_dep.found()
    cmake = import('cmake')
    opt_var = cmake.subproject_options()
    opt_var.add_cmake_defines({'CMAKE_CXX_FLAGS': '-Wno-pedantic'})
    opt_var.set_override_option('warning_level', '3')
    opt_var.set_override_option('werror', 'true')

    gtest_proj = cmake.subproject('googletest', required: false)
    if gtest_proj.found()
        gtest_dep = declare_dependency(
            dependencies: [
                dependency('threads'),
                gtest_proj.dependency('gtest'),
            ],
        )
        gmock_dep = gtest_proj.dependency('gmock')
    else
        assert(
            not get_option('tests').enabled(),
            'Googletest is required if tests are enabled',
        )
    endif
endif

if get_option('tests').allowed()
    test(
        'test_host_error_monitor',
        executable(
            'test_host_error_monitor',
            [
                'test_main.cpp',
                'dbus_environment.cpp',
                'mock_gpiod.cpp',
                'test_host_error_monitor.cpp',
                'test_error_monitors.cpp',
                'test_base_gpio_monitor.cpp',
                'test_base_gpio_poll_monitor.cpp',
                'test_cpld_crc_monitor.cpp',
                'test_cpu_early_error_monitor.cpp',
                'test_cpu_mismatch_monitor.cpp',
                'test_cpu_presence_monitor.cpp',
                'test_cpu_thermtrip_monitor.cpp',
                'test_err_pin_monitor.cpp',
                'test_err_pin_timeout_monitor.cpp',
                'test_err2_monitor.cpp',
                'test_ierr_monitor.cpp',
                'test_mcerr_monitor.cpp',
                'test_mem_thermtrip_monitor.cpp',
                'test_memhot_monitor.cpp',
                'test_pch_thermtrip_monitor.cpp',
                'test_prochot_monitor.cpp',
                'test_smi_monitor.cpp',
                'test_vr_hot_monitor.cpp',
                '../src/host_error_monitor.cpp',
            ],
            cpp_args: ['-DUNIT_TEST'],
            include_directories: [incs, include_directories('.')],
            dependencies: deps + [gtest_dep, gmock_dep],
        ),
        timeout: 180,
    )
endif
