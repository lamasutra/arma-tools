add_executable(wrp_info main.cpp)
target_link_libraries(wrp_info PRIVATE armatools::wrp armatools::shp armatools::config armatools::tb nlohmann_json::nlohmann_json armatools_cli_logger)
armatools_set_warnings(wrp_info)
install(TARGETS wrp_info RUNTIME DESTINATION bin)
