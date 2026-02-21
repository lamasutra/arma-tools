add_library(armatools_shp src/shp.cpp)
add_library(armatools::shp ALIAS armatools_shp)

target_include_directories(armatools_shp PUBLIC include)
armatools_set_warnings(armatools_shp)
