add_library(armatools_config src/config.cpp)
add_library(armatools::config ALIAS armatools_config)

target_include_directories(armatools_config PUBLIC include)
target_link_libraries(armatools_config PUBLIC armatools::binutil)
armatools_set_warnings(armatools_config)
