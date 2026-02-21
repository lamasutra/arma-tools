add_library(armatools_ogg src/ogg.cpp)
add_library(armatools::ogg ALIAS armatools_ogg)

target_include_directories(armatools_ogg PUBLIC include)
target_link_libraries(armatools_ogg PUBLIC armatools::binutil)
armatools_set_warnings(armatools_ogg)
