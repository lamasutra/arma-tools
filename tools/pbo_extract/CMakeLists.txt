add_executable(pbo_extract main.cpp)
target_link_libraries(pbo_extract PRIVATE armatools::pbo armatools::armapath armatools::config)
armatools_set_warnings(pbo_extract)
install(TARGETS pbo_extract RUNTIME DESTINATION bin)
