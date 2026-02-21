set(PBOINDEX_SQLITE_TARGET "")

# For cross-compiling (for example MinGW on Linux), host SQLite is not usable.
# Build bundled SQLite amalgamation in that case.
if(CMAKE_CROSSCOMPILING)
    include(FetchContent)
    set(SQLITE_AMALGAMATION_VERSION 3450100)
    FetchContent_Declare(
        sqlite3_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-${SQLITE_AMALGAMATION_VERSION}.zip
    )
    FetchContent_MakeAvailable(sqlite3_amalgamation)

    add_library(armatools_sqlite3 STATIC
        ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c
    )
    target_include_directories(armatools_sqlite3 PUBLIC
        ${sqlite3_amalgamation_SOURCE_DIR}
    )
    target_compile_definitions(armatools_sqlite3 PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION=1
    )
    set(PBOINDEX_SQLITE_TARGET armatools_sqlite3)
else()
    find_package(SQLite3 REQUIRED)
    set(PBOINDEX_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(armatools_pboindex src/pboindex.cpp)
add_library(armatools::pboindex ALIAS armatools_pboindex)

target_include_directories(armatools_pboindex PUBLIC include)
target_link_libraries(armatools_pboindex
    PUBLIC
        armatools::armapath
        armatools::pbo
        armatools::p3d
        armatools::paa
        armatools::ogg
        armatools::wss
        ${PBOINDEX_SQLITE_TARGET}
)
armatools_set_warnings(armatools_pboindex)
