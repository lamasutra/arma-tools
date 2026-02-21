add_library(armatools_binutil src/binutil.cpp)
add_library(armatools::binutil ALIAS armatools_binutil)

target_include_directories(armatools_binutil PUBLIC include)
armatools_set_warnings(armatools_binutil)
