find_package(Threads REQUIRED)

add_library(http_core STATIC request_parser.cpp server.cpp)
target_include_directories(http_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(http_core PUBLIC Threads::Threads PkgConfig::LIBURING)

add_executable(http_server main.cpp)
target_link_libraries(http_server PRIVATE http_core)
