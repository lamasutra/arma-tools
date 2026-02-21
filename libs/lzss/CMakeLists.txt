add_library(armatools_lzss src/lzss.cpp)
add_library(armatools::lzss ALIAS armatools_lzss)

target_include_directories(armatools_lzss PUBLIC include)
target_link_libraries(armatools_lzss PUBLIC armatools::binutil)
armatools_set_warnings(armatools_lzss)
