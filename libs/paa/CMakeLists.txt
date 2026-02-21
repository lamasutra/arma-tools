add_library(armatools_paa src/paa.cpp)
add_library(armatools::paa ALIAS armatools_paa)

target_include_directories(armatools_paa PUBLIC include)
target_link_libraries(armatools_paa PUBLIC armatools::binutil armatools::lzss)
armatools_set_warnings(armatools_paa)
