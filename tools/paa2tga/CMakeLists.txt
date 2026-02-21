add_executable(paa2tga main.cpp)
target_link_libraries(paa2tga PRIVATE armatools::paa armatools::tga)
armatools_set_warnings(paa2tga)
install(TARGETS paa2tga RUNTIME DESTINATION bin)
