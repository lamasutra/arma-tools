add_library(armatools_armapath src/armapath.cpp)
add_library(armatools::armapath ALIAS armatools_armapath)

target_include_directories(armatools_armapath PUBLIC include)
armatools_set_warnings(armatools_armapath)
