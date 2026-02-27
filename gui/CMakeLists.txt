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
file(GLOB_RECURSE GUI_GRESOURCE_DEPENDS
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/css/*
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/data/shaders/*
)
add_custom_command(
    OUTPUT ${GUI_GRESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
        ${GUI_GRESOURCE_XML}
        --target=${GUI_GRESOURCE_C}
        --generate-source
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/resources
    DEPENDS
        ${GUI_GRESOURCE_XML}
        ${GUI_GRESOURCE_DEPENDS}
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

set(RENDERER_PLUGIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/plugins/renderers)
set(UI_PLUGIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/plugins/ui)

add_library(renderer_null MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/render_domain_plugins/renderer_null.cpp
)
target_include_directories(renderer_null PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(renderer_null PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
)

add_library(renderer_gles MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/render_domain_plugins/renderer_gles.cpp
)
target_include_directories(renderer_gles PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(renderer_gles PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
)

if(WIN32)
    add_library(renderer_dx9 MODULE
        ${CMAKE_CURRENT_SOURCE_DIR}/render_domain_plugins/renderer_dx9.cpp
    )
    target_include_directories(renderer_dx9 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(renderer_dx9 PRIVATE d3d9)
    set_target_properties(renderer_dx9 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${RENDERER_PLUGIN_OUTPUT_DIR}
    )
endif()

add_library(ui_null MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_domain_plugins/ui_null.cpp
)
target_include_directories(ui_null PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(ui_null PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
)

add_library(ui_gtk MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_domain_plugins/ui_gtk.cpp
)
target_include_directories(ui_gtk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(ui_gtk PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
)

add_library(ui_imgui MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_domain_plugins/ui_imgui.cpp
)
target_include_directories(ui_imgui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(ui_imgui PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${UI_PLUGIN_OUTPUT_DIR}
)

add_dependencies(arma-tools renderer_null renderer_gles)
if(WIN32)
    add_dependencies(arma-tools renderer_dx9)
endif()
add_dependencies(arma-tools ui_null ui_gtk ui_imgui)

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
    armatools::rvmat
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
install(TARGETS renderer_null renderer_gles
    LIBRARY DESTINATION bin/plugins/renderers
    RUNTIME DESTINATION bin/plugins/renderers
)
if(WIN32)
    install(TARGETS renderer_dx9
        LIBRARY DESTINATION bin/plugins/renderers
        RUNTIME DESTINATION bin/plugins/renderers
    )
endif()
install(TARGETS ui_null ui_gtk ui_imgui
    LIBRARY DESTINATION bin/plugins/ui
    RUNTIME DESTINATION bin/plugins/ui
)
