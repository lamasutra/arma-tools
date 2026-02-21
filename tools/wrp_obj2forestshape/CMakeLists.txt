add_executable(wrp_obj2forestshape main.cpp)
target_link_libraries(wrp_obj2forestshape PRIVATE armatools::wrp armatools::forestshape nlohmann_json::nlohmann_json)
armatools_set_warnings(wrp_obj2forestshape)
install(TARGETS wrp_obj2forestshape RUNTIME DESTINATION bin)
