add_library(armatools_roadnet src/roadnet.cpp)
add_library(armatools::roadnet ALIAS armatools_roadnet)

target_include_directories(armatools_roadnet PUBLIC include)
target_link_libraries(armatools_roadnet PUBLIC armatools::wrp)
armatools_set_warnings(armatools_roadnet)
