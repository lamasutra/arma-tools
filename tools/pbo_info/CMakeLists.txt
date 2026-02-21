add_executable(pbo_info main.cpp)
target_link_libraries(pbo_info PRIVATE armatools::pbo nlohmann_json::nlohmann_json armatools_cli_logger)
armatools_set_warnings(pbo_info)
install(TARGETS pbo_info RUNTIME DESTINATION bin)
