add_executable(cadd0040_tests
    test_app.cpp
    test_clock_tree.cpp
    test_parser.cpp
    test_datapath_parser.cpp
)

target_link_libraries(cadd0040_tests
    PRIVATE
        cadd0040_core
        Catch2::Catch2WithMain
)

target_compile_definitions(cadd0040_tests
    PRIVATE
        CADD0040_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

include(Catch)
catch_discover_tests(cadd0040_tests)
