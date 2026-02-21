add_library(armatools_wss src/wss.cpp)
add_library(armatools::wss ALIAS armatools_wss)

target_include_directories(armatools_wss PUBLIC include)
target_link_libraries(armatools_wss PUBLIC armatools::binutil)
armatools_set_warnings(armatools_wss)
