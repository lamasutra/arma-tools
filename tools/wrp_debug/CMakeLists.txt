add_executable(wrp_debug main.cpp)
target_link_libraries(wrp_debug PRIVATE armatools::binutil armatools::lzss armatools::lzo)
armatools_set_warnings(wrp_debug)
