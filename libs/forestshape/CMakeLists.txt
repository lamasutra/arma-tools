add_library(armatools_forestshape src/forestshape.cpp)
add_library(armatools::forestshape ALIAS armatools_forestshape)

target_include_directories(armatools_forestshape PUBLIC include)
target_link_libraries(armatools_forestshape PUBLIC armatools::wrp)
armatools_set_warnings(armatools_forestshape)
