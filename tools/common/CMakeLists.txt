add_library(armatools_cli_logger INTERFACE)
target_include_directories(armatools_cli_logger INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(armatools_cli_logger INTERFACE consoleu)
target_compile_definitions(armatools_cli_logger INTERFACE ARMA_TOOLS_CLI_LOGGER=1)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cli_logger.h
        DESTINATION include/arma-tools/tools)
