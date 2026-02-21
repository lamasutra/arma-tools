add_library(armatools_tb src/tb.cpp)
add_library(armatools::tb ALIAS armatools_tb)

target_include_directories(armatools_tb PUBLIC include)
armatools_set_warnings(armatools_tb)
