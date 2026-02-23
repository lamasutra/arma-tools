add_library(armatools_rvmat src/rvmat.cpp)
add_library(armatools::rvmat ALIAS armatools_rvmat)

target_include_directories(armatools_rvmat PUBLIC include)
target_link_libraries(armatools_rvmat PUBLIC armatools::config)
armatools_set_warnings(armatools_rvmat)
