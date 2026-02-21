add_library(armatools_roadobj src/roadobj.cpp)
add_library(armatools::roadobj ALIAS armatools_roadobj)

target_include_directories(armatools_roadobj PUBLIC include)
armatools_set_warnings(armatools_roadobj)
