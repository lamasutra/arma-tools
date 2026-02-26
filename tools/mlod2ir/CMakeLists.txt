add_executable(mlod2ir main.cpp)
target_link_libraries(mlod2ir PRIVATE
    armatools::p3d
    nlohmann_json::nlohmann_json
    armatools_cli_logger
)
armatools_set_warnings(mlod2ir)
install(TARGETS mlod2ir RUNTIME DESTINATION bin)
