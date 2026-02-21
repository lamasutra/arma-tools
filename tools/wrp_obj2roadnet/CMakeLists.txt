add_executable(wrp_obj2roadnet main.cpp)
target_link_libraries(wrp_obj2roadnet PRIVATE armatools::wrp armatools::roadnet nlohmann_json::nlohmann_json)
armatools_set_warnings(wrp_obj2roadnet)
install(TARGETS wrp_obj2roadnet RUNTIME DESTINATION bin)
