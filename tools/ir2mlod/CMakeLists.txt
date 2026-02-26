add_executable(ir2mlod main.cpp)
target_link_libraries(ir2mlod PRIVATE
    armatools::binutil
    nlohmann_json::nlohmann_json
    armatools_cli_logger
)
armatools_set_warnings(ir2mlod)
install(TARGETS ir2mlod RUNTIME DESTINATION bin)
