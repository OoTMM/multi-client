file(GLOB_RECURSE SOURCES "*.c")
include_directories("${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src")

add_executable(MultiClient ${SOURCES})
if(WIN32)
    target_link_libraries(MultiClient Ws2_32.lib)
endif()
install(TARGETS MultiClient DESTINATION .)
