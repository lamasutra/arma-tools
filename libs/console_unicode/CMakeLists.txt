add_library(consoleu STATIC console_unicode.cpp)
target_include_directories(consoleu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(consoleu
    PUBLIC
        armatools_version
)
