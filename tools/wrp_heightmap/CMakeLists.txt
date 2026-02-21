add_executable(wrp_heightmap main.cpp)
target_link_libraries(wrp_heightmap PRIVATE armatools::wrp)
armatools_set_warnings(wrp_heightmap)
install(TARGETS wrp_heightmap RUNTIME DESTINATION bin)
