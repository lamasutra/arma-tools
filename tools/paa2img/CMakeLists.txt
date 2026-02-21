add_executable(paa2img main.cpp)
target_link_libraries(paa2img PRIVATE armatools::paa stb_image_write)
armatools_set_warnings(paa2img)
install(TARGETS paa2img RUNTIME DESTINATION bin)
