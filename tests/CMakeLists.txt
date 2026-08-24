# The suite compiles the mod's own translation units rather than copies of them:
# MOD_TESTABLE_SOURCES (set in the root CMakeLists) is exactly the half whose
# behaviour is decidable without a game process. The rest - the detours, the
# cursor hook, the hotkey poller - only does anything against a live hook.
add_executable(kcd_tests
    test_main.cpp
    view_injection_tests.cpp
    aim_projection_tests.cpp
    pose_channel_tests.cpp
    config_tests.cpp
    build_profile_tests.cpp
    game_state_tests.cpp
    ${MOD_TESTABLE_SOURCES}
)

target_include_directories(kcd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)

target_link_libraries(kcd_tests PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(kcd_tests PRIVATE /W4 /WX)
endif()

add_test(NAME kcd_tests COMMAND kcd_tests)
