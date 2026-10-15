find_package(Catch2 3 CONFIG REQUIRED)

add_executable(windeck_tests
    dead_zone_test.cpp
    stick_mapper_test.cpp
    inertia_test.cpp
    profile_json_test.cpp
)

target_link_libraries(windeck_tests PRIVATE
    windeck_core
    Catch2::Catch2WithMain
)

# The example profiles shipped in resources/ must keep loading.
target_compile_definitions(windeck_tests PRIVATE
    WINDECK_PROFILES_DIR="${PROJECT_SOURCE_DIR}/resources/profiles"
)

include(Catch)
catch_discover_tests(windeck_tests)
