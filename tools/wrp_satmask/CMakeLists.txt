add_executable(wrp_satmask main.cpp)
target_link_libraries(wrp_satmask PRIVATE armatools::wrp armatools::surface nlohmann_json::nlohmann_json)
armatools_set_warnings(wrp_satmask)
install(TARGETS wrp_satmask RUNTIME DESTINATION bin)
