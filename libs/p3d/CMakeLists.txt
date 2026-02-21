add_library(armatools_p3d src/p3d.cpp)
add_library(armatools::p3d ALIAS armatools_p3d)

target_include_directories(armatools_p3d PUBLIC include)
target_link_libraries(armatools_p3d PUBLIC armatools::binutil armatools::lzss armatools::lzo)
armatools_set_warnings(armatools_p3d)
