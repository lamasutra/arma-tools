add_executable(wrp_objreplace main.cpp)
target_link_libraries(wrp_objreplace PRIVATE armatools::wrp armatools::roadobj armatools::tb nlohmann_json::nlohmann_json)
armatools_set_warnings(wrp_objreplace)
install(TARGETS wrp_objreplace RUNTIME DESTINATION bin)
