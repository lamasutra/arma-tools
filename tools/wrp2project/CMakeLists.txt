option(WRP2PROJECT_WITH_TV4L "Build wrp2project with TV4L generation support" ON)

set(WRP2PROJECT_SOURCES
    main.cpp
    generators.cpp
    tv4p.cpp
    tv4s.cpp
    v4d.cpp
)

if(WRP2PROJECT_WITH_TV4L)
    list(APPEND WRP2PROJECT_SOURCES tv4l.cpp)
endif()

add_executable(wrp2project ${WRP2PROJECT_SOURCES})

set(WRP2PROJECT_ZLIB_TARGET "")
if(CMAKE_CROSSCOMPILING)
    include(FetchContent)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zlib_src
        URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
    )
    FetchContent_MakeAvailable(zlib_src)
    if(TARGET zlibstatic)
        set(WRP2PROJECT_ZLIB_TARGET zlibstatic)
    elseif(TARGET zlib)
        set(WRP2PROJECT_ZLIB_TARGET zlib)
    endif()
else()
    find_package(ZLIB REQUIRED)
    set(WRP2PROJECT_ZLIB_TARGET ZLIB::ZLIB)
endif()

target_link_libraries(wrp2project PRIVATE
    armatools::wrp
    armatools::roadobj
    armatools::roadnet
    armatools::forestshape
    armatools::surface
    armatools::objcat
    armatools::tb
    armatools::shp
    armatools::config
    armatools::armapath
    armatools::pboindex
    armatools::pbo
    nlohmann_json::nlohmann_json
    ${WRP2PROJECT_ZLIB_TARGET}
)
target_include_directories(wrp2project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WRP2PROJECT_WITH_TV4L)
    target_compile_definitions(wrp2project PRIVATE WRP2PROJECT_WITH_TV4L=1)
endif()
armatools_set_warnings(wrp2project)
install(TARGETS wrp2project RUNTIME DESTINATION bin)
