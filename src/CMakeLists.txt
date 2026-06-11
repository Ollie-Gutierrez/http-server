add_library(http_core STATIC request_parser.cpp)
target_include_directories(http_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(http_server main.cpp)
target_link_libraries(http_server PRIVATE http_core)
