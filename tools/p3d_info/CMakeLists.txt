add_executable(p3d_info main.cpp)
target_link_libraries(p3d_info PRIVATE
    armatools::armapath
    armatools::config
    armatools::p3d
    armatools::pbo
    armatools::pboindex
    armatools::rvmat
    nlohmann_json::nlohmann_json
    armatools_cli_logger
)
armatools_set_warnings(p3d_info)
install(TARGETS p3d_info RUNTIME DESTINATION bin)
