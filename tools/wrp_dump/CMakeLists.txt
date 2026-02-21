add_executable(wrp_dump main.cpp)
target_link_libraries(wrp_dump PRIVATE armatools::wrp nlohmann_json::nlohmann_json armatools_cli_logger)
armatools_set_warnings(wrp_dump)
install(TARGETS wrp_dump RUNTIME DESTINATION bin)
