add_library(armatools_surface src/surface.cpp)
add_library(armatools::surface ALIAS armatools_surface)

target_include_directories(armatools_surface PUBLIC include)
armatools_set_warnings(armatools_surface)
