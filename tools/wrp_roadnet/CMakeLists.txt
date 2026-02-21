add_executable(wrp_roadnet main.cpp)
target_link_libraries(wrp_roadnet PRIVATE armatools::wrp armatools::roadnet nlohmann_json::nlohmann_json)
armatools_set_warnings(wrp_roadnet)
install(TARGETS wrp_roadnet RUNTIME DESTINATION bin)
