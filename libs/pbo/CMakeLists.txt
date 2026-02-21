add_library(armatools_pbo src/pbo.cpp)
add_library(armatools::pbo ALIAS armatools_pbo)

target_include_directories(armatools_pbo PUBLIC include)
target_link_libraries(armatools_pbo PUBLIC armatools::binutil armatools::lzss)
armatools_set_warnings(armatools_pbo)
