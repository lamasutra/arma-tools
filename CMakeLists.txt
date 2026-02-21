cmake_minimum_required(VERSION 3.24)
project(arma-tools VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build unit tests" ON)
option(BUILD_GUI "Build GTK4 GUI application" OFF)

include(cmake/CompilerWarnings.cmake)
include(FetchContent)

# --- Version header (regenerated on every build to track git state) ---
add_custom_target(armatools_version_gen ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        -DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        -DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    COMMENT "Generating version header"
)
add_library(armatools_version INTERFACE)
target_include_directories(armatools_version INTERFACE ${CMAKE_BINARY_DIR}/generated)
add_dependencies(armatools_version armatools_version_gen)

# --- Third-party dependencies ---

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f1c79c02822848a9bed4315b12c8c8f3761e1296
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# stb_image_write as a header-only interface library
add_library(stb_image_write INTERFACE)
target_include_directories(stb_image_write SYSTEM INTERFACE ${stb_SOURCE_DIR})

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

# miniaudio as a header-only interface library
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

if(BUILD_TESTING)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

# --- Libraries ---
add_subdirectory(libs)

# --- CLI tools ---
add_subdirectory(tools)

# --- Tests ---
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# --- GUI (optional) ---
if(BUILD_GUI)
    add_subdirectory(gui)
endif()

# --- Desktop integration (Linux) ---
if(UNIX AND NOT APPLE AND BUILD_GUI)
    install(FILES assets/arma-tools.desktop
        DESTINATION share/applications)
    install(FILES assets/arma-tools.png
        DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES assets/arma-tools-128.png
        DESTINATION share/icons/hicolor/128x128/apps
        RENAME arma-tools.png)
    install(FILES assets/arma-tools-64.png
        DESTINATION share/icons/hicolor/64x64/apps
        RENAME arma-tools.png)
endif()

# --- Man pages (Linux CLI tools) ---
if(UNIX AND NOT APPLE)
    install(
        DIRECTORY docs/man/man1/
        DESTINATION share/man/man1
        FILES_MATCHING
        PATTERN "*.1"
    )
endif()

# --- Packaging ---
install(FILES
    ${CMAKE_SOURCE_DIR}/LICENSE.md
    ${CMAKE_SOURCE_DIR}/LEGAL.md
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/arma-tools")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/arma-tools")
set(CPACK_PACKAGE_NAME "arma-tools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Arma modding tools (CLI and GUI)")
set(CPACK_PACKAGE_CONTACT "andrzej")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/arma-tools.png")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")

    set(CPACK_NSIS_DISPLAY_NAME "Arma Tools ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "Arma Tools")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/arma-tools.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/arma-tools.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\arma-tools.exe")

    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Arma Tools.lnk' '$INSTDIR\\\\bin\\\\arma-tools.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU\\\\Arma Tools.lnk'")

    # Bundle runtime DLLs for all installed Windows executables (CLI + GUI).
    install(CODE "
        set(_dst_bin \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        set(_dst_share \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share\")
        file(GLOB _exe_files \"\${_dst_bin}/*.exe\")
        if(NOT _exe_files)
            message(STATUS \"No installed Windows executables found for runtime dependency bundling.\")
            return()
        endif()

        set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM \"windows+pe\")
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL \"objdump\")
        if(EXISTS \"/usr/bin/x86_64-w64-mingw32ucrt-objdump\")
            set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"/usr/bin/x86_64-w64-mingw32ucrt-objdump\")
        elseif(EXISTS \"/usr/bin/x86_64-w64-mingw32-objdump\")
            set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"/usr/bin/x86_64-w64-mingw32-objdump\")
        endif()

        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \${_exe_files}
            RESOLVED_DEPENDENCIES_VAR _deps
            UNRESOLVED_DEPENDENCIES_VAR _udeps
            DIRECTORIES
                /ucrt64/bin
                /usr/x86_64-w64-mingw32/bin
                /usr/lib/gcc/x86_64-w64-mingw32ucrt/14-posix
                /usr/lib/gcc/x86_64-w64-mingw32/14-posix
            PRE_EXCLUDE_REGEXES
                \"api-ms-.*\"
                \"ext-ms-.*\"
            POST_EXCLUDE_REGEXES
                \".*/[Ss]ystem32/.*\"
        )

        foreach(_dep IN LISTS _deps)
            file(INSTALL DESTINATION \"\${_dst_bin}\" TYPE SHARED_LIBRARY
                 FOLLOW_SYMLINK_CHAIN FILES \"\${_dep}\")
        endforeach()

        # CMake's dependency resolver occasionally misses libLerc.dll on cross-builds.
        foreach(_lerc_candidate IN ITEMS
            /ucrt64/bin/libLerc.dll
            /opt/msys2/ucrt64/bin/libLerc.dll
        )
            if(EXISTS \"\${_lerc_candidate}\")
                file(INSTALL DESTINATION \"\${_dst_bin}\" TYPE SHARED_LIBRARY
                     FOLLOW_SYMLINK_CHAIN FILES \"\${_lerc_candidate}\")
                break()
            endif()
        endforeach()

        # Bundle GLib schemas required by GTK/libadwaita at runtime.
        set(_schema_src_dir \"\")
        foreach(_schema_candidate IN ITEMS
            /ucrt64/share/glib-2.0/schemas
            /opt/msys2/ucrt64/share/glib-2.0/schemas
        )
            if(EXISTS \"\${_schema_candidate}\")
                set(_schema_src_dir \"\${_schema_candidate}\")
                break()
            endif()
        endforeach()
        if(_schema_src_dir)
            file(GLOB _schema_xml \"\${_schema_src_dir}/*.gschema.xml\")
            if(_schema_xml)
                file(INSTALL DESTINATION \"\${_dst_share}/glib-2.0/schemas\" TYPE FILE
                     FILES \${_schema_xml})
            endif()
            if(EXISTS \"\${_schema_src_dir}/gschema.dtd\")
                file(INSTALL DESTINATION \"\${_dst_share}/glib-2.0/schemas\" TYPE FILE
                     FILES \"\${_schema_src_dir}/gschema.dtd\")
            endif()

            find_program(_glib_compile_schemas glib-compile-schemas)
            if(_glib_compile_schemas)
                execute_process(
                    COMMAND \"\${_glib_compile_schemas}\" \"\${_dst_share}/glib-2.0/schemas\"
                    RESULT_VARIABLE _gschemas_rv
                    OUTPUT_QUIET
                    ERROR_VARIABLE _gschemas_err
                )
                if(NOT _gschemas_rv EQUAL 0)
                    message(WARNING \"glib-compile-schemas failed: \${_gschemas_err}\")
                endif()
            elseif(EXISTS \"\${_schema_src_dir}/gschemas.compiled\")
                # Fallback for environments where only precompiled schema cache exists.
                file(INSTALL DESTINATION \"\${_dst_share}/glib-2.0/schemas\" TYPE FILE
                     FILES \"\${_schema_src_dir}/gschemas.compiled\")
            endif()
        endif()

        if(_udeps)
            message(WARNING \"Unresolved runtime dependencies: \${_udeps}\")
        endif()
    ")
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
