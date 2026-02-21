add_library(armatools_wrp src/wrp.cpp)
add_library(armatools::wrp ALIAS armatools_wrp)

target_include_directories(armatools_wrp PUBLIC include)
target_link_libraries(armatools_wrp PUBLIC armatools::binutil armatools::lzss armatools::lzo)
armatools_set_warnings(armatools_wrp)
