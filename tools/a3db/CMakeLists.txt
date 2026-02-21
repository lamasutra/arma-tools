add_executable(a3db main.cpp)
target_link_libraries(a3db PRIVATE armatools::pboindex nlohmann_json::nlohmann_json)
armatools_set_warnings(a3db)
install(TARGETS a3db RUNTIME DESTINATION bin)
