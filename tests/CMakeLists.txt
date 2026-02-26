include(GoogleTest)

function(armatools_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    armatools_set_warnings(${name})
    gtest_discover_tests(${name})
endfunction()

# Per-library tests
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/binutil/test ${CMAKE_CURRENT_BINARY_DIR}/binutil_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/lzss/test ${CMAKE_CURRENT_BINARY_DIR}/lzss_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/lzo/test ${CMAKE_CURRENT_BINARY_DIR}/lzo_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/pbo/test ${CMAKE_CURRENT_BINARY_DIR}/pbo_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/heightpipe/test ${CMAKE_CURRENT_BINARY_DIR}/heightpipe_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/rvmat/test ${CMAKE_CURRENT_BINARY_DIR}/rvmat_test)

armatools_add_test(spec_validation_tests spec_validation_tests.cpp)
target_compile_definitions(spec_validation_tests PRIVATE ARMATOOLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

armatools_add_test(ir2mlod_roundtrip_tests ir2mlod_roundtrip_tests.cpp)
target_link_libraries(ir2mlod_roundtrip_tests PRIVATE armatools::p3d nlohmann_json::nlohmann_json)
add_dependencies(ir2mlod_roundtrip_tests ir2mlod mlod2ir)
target_compile_definitions(ir2mlod_roundtrip_tests PRIVATE
    ARMATOOLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ARMATOOLS_BINARY_DIR="${CMAKE_BINARY_DIR}")
