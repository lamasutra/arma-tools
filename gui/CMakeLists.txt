find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(LIBPANEL REQUIRED IMPORTED_TARGET libpanel-1)
pkg_check_modules(LIBADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

file(GLOB_RECURSE GUI_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

set(GUI_GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/resources/arma-tools.gresource.xml)
set(GUI_GRESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/arma-tools-resources.c)
add_custom_command(
    OUTPUT ${GUI_GRESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
        ${GUI_GRESOURCE_XML}
        --target=${GUI_GRESOURCE_C}
        --generate-source
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/resources
    DEPENDS
        ${GUI_GRESOURCE_XML}
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/css/style.css
    VERBATIM
)

if(WIN32)
    set(ARMA_TOOLS_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/arma-tools.ico")
    cmake_path(NATIVE_PATH ARMA_TOOLS_ICON_PATH ARMA_TOOLS_ICON_PATH_NATIVE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/arma-tools.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/arma-tools.rc
        @ONLY
    )
endif()

add_executable(arma-tools
    ${GUI_SOURCES}
    ${GUI_GRESOURCE_C}
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_BINARY_DIR}/arma-tools.rc>
)

target_include_directories(arma-tools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tabs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/tools/common
    ${CMAKE_SOURCE_DIR}/libs/console_unicode
)

# Suppress warnings in miniaudio (third-party header-only lib)
set_source_files_properties(src/miniaudio_impl.c PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(arma-tools PRIVATE
    PkgConfig::GTKMM
    PkgConfig::EPOXY
    PkgConfig::LIBPANEL
    PkgConfig::LIBADWAITA
    armatools::paa
    armatools::wrp
    armatools::pbo
    armatools::p3d
    armatools::ogg
    armatools::wss
    armatools::tga
    armatools::config
    armatools::pboindex
    armatools::objcat
    armatools::heightpipe
    nlohmann_json::nlohmann_json
    miniaudio
    PkgConfig::VORBISFILE
    armatools_version
    consoleu
)

if(WRP2PROJECT_WITH_TV4L)
    target_compile_definitions(arma-tools PRIVATE WRP2PROJECT_WITH_TV4L=1)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(arma-tools PRIVATE Threads::Threads m dl)
endif()

armatools_set_warnings(arma-tools)
if(WIN32)
    set_target_properties(arma-tools PROPERTIES WIN32_EXECUTABLE ON)
endif()
install(TARGETS arma-tools RUNTIME DESTINATION bin)
