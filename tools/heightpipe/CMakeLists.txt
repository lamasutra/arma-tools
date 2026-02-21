add_executable(heightpipe main.cpp)
target_link_libraries(heightpipe PRIVATE armatools::heightpipe)
armatools_set_warnings(heightpipe)
install(TARGETS heightpipe RUNTIME DESTINATION bin)
