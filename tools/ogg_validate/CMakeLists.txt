add_executable(ogg_validate main.cpp)
target_link_libraries(ogg_validate PRIVATE armatools::ogg armatools::pbo nlohmann_json::nlohmann_json)
armatools_set_warnings(ogg_validate)
install(TARGETS ogg_validate RUNTIME DESTINATION bin)
