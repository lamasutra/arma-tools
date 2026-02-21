add_library(armatools_tga src/tga.cpp)
add_library(armatools::tga ALIAS armatools_tga)

target_include_directories(armatools_tga PUBLIC include)
target_link_libraries(armatools_tga PUBLIC armatools::binutil)
armatools_set_warnings(armatools_tga)
