find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE http_core GTest::gtest_main)
gtest_discover_tests(test_parser)
