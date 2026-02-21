add_library(armatools_objcat src/objcat.cpp)
add_library(armatools::objcat ALIAS armatools_objcat)

target_include_directories(armatools_objcat PUBLIC include)
target_link_libraries(armatools_objcat PUBLIC armatools::armapath)
armatools_set_warnings(armatools_objcat)
