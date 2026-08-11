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
    tst_cpudetector
    tst_kscreendoctor
    tst_processrunner
    tst_game
    tst_steamlauncher
    tst_launchermanager
    tst_launchplan
    tst_badgerow
    tst_storevisuals
    tst_secretstore
    tst_gogauth
    tst_gogapi
    tst_steamstore
    tst_gogcontent
    tst_gogplan
    tst_gogplaytasks
    tst_gogregistry
    tst_gogchunks
    tst_gogzip
    tst_gogoffline
    tst_gogqueue
)

foreach(test IN LISTS UNIT_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE protonforge_core Qt6::Test)
    # Captured real output of the external tools, shared with the lab's stubs —
    # one copy, so a test and a stub can never drift apart.
    target_compile_definitions(${test} PRIVATE
        PROTONFORGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../steam-lab/fixtures")
    add_test(NAME ${test} COMMAND ${test})
    # No display anywhere in CI, and none of these need one.
    set_tests_properties(${test} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
endforeach()
