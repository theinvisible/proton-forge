# Unit tests. Enabled with -DPROTONFORGE_BUILD_TESTS=ON.
#
# One executable per subject, each linking the real protonforge_core — there
# are no mocks and no second copy of the code under test. Everything here is
# pure: no network, no display, no Steam, no GPU. Anything that needs those
# lives in tests/steam-lab/ instead.

set(UNIT_TESTS
    tst_envbuilder
    tst_vdfparser
    tst_featuregate
    tst_dlsssettings
    tst_protondbid
    tst_launchoptionextractor
    tst_steampaths
    tst_gpudetector
)

foreach(test IN LISTS UNIT_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE protonforge_core Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
    # No display anywhere in CI, and none of these need one.
    set_tests_properties(${test} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
endforeach()
