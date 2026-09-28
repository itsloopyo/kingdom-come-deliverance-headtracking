# The differential test: the published build's reader (oracle/), the frozen
# reader in src/legacy_config/, and every input the corpus generator makes from
# the published build's first-run file.
#
# The oracle is compiled from published/ with `cameraunlock` and `kcd_ht`
# renamed by the preprocessor. It needs core as it stood at the published
# build's pin, whose headers share their paths and names with the current ones,
# so the two can only live in one binary under different names.
add_library(kcd_config_oracle STATIC
    oracle/oracle.cpp
    oracle/published/src/config.cpp
    oracle/published/core/src/config/ini_reader.cpp
    oracle/published/core/src/logging/file_log.cpp
)
target_include_directories(kcd_config_oracle PRIVATE
    oracle
    oracle/published/src
    oracle/published/core/include
)
target_compile_definitions(kcd_config_oracle PRIVATE
    cameraunlock=kcd_oracle_cameraunlock
    kcd_ht=kcd_oracle_kcd_ht
)

add_executable(kcd_config_differential
    differential_tests.cpp
    ${MOD_TESTABLE_SOURCES}
)
target_include_directories(kcd_config_differential PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_compile_definitions(kcd_config_differential PRIVATE
    KCD_DIFFERENTIAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    KCD_REPO_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(kcd_config_differential PRIVATE cameraunlock kcd_config_oracle bcrypt)
if(MSVC)
    target_compile_options(kcd_config_differential PRIVATE /W4 /WX /bigobj)
endif()

add_test(NAME config_differential COMMAND kcd_config_differential)

# Core's canonical config lint over the committed file and every file the
# differential test migrated, which it writes beside itself under migrated\.
add_test(NAME config_lint COMMAND node
    ${CMAKE_CURRENT_SOURCE_DIR}/lint-migrated.mjs
    $<TARGET_FILE_DIR:kcd_config_differential>/migrated)
set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)
set_tests_properties(config_differential config_lint PROPERTIES LABELS differential)
