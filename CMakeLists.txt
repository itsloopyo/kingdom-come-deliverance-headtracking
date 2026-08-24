cmake_minimum_required(VERSION 3.20)
project(KingdomComeDeliveranceHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# MinHook (for the CView::Update trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
#
# Pinned to the COMMIT, not the tag. A git tag is a mutable ref: whoever holds
# the upstream repo can move v1.3.3 to different code and every clean build
# here - including the CI job that publishes the release ZIPs - links it into
# the shipped .asi with nothing to notice. The same rule we apply to pinning
# GitHub Actions, for the same reason.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# The mod is split by what a test can decide. MOD_TESTABLE_SOURCES is compiled
# into the test executable as well as the DLL, so the suite runs against the same
# translation units that ship rather than a copy of them; the runtime half only
# does anything against a live hook in a running game. Absolute paths: tests/
# consumes this list from its own directory.
set(MOD_TESTABLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aim_projection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exe_paths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pose_channel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view_injection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builds/steam_offsets.cpp
)

set(MOD_RUNTIME_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dllmain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/headtracking_mod.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cursor_hook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fov_override.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hotkeys.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view_hooks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builds/build_registry.cpp
)

add_library(KingdomComeDeliveranceHeadTracking SHARED
    ${MOD_TESTABLE_SOURCES}
    ${MOD_RUNTIME_SOURCES}
)

target_include_directories(KingdomComeDeliveranceHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(KingdomComeDeliveranceHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(KingdomComeDeliveranceHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Characterization tests for the parts of the injection that are decidable
# without a game process: the camera maths, the INI contract and the build
# registry. `pixi run test` builds and runs them.
option(KCD_BUILD_TESTS "Build unit tests" ON)
if(KCD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
