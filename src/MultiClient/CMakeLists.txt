file(GLOB_RECURSE SOURCES "*.c")
include_directories("${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src")

add_executable(MultiClient ${SOURCES})
target_link_libraries(MultiClient Ws2_32.lib)
install(TARGETS MultiClient DESTINATION .)
