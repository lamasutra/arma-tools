add_library(armatools_lzo src/lzo.cpp)
add_library(armatools::lzo ALIAS armatools_lzo)

target_include_directories(armatools_lzo PUBLIC include)
target_link_libraries(armatools_lzo PUBLIC armatools::binutil)
armatools_set_warnings(armatools_lzo)
