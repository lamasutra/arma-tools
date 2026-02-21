add_executable(tga2paa main.cpp)
target_link_libraries(tga2paa PRIVATE armatools::paa armatools::tga)
armatools_set_warnings(tga2paa)
install(TARGETS tga2paa RUNTIME DESTINATION bin)
