add_executable(audio_player main.cpp miniaudio_impl.c)
target_link_libraries(audio_player PRIVATE armatools::wss miniaudio)

# Apply warnings only to C++ sources (miniaudio_impl.c is third-party C code)
set_source_files_properties(miniaudio_impl.c PROPERTIES COMPILE_OPTIONS "-w")
if(MSVC)
    set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS
        "/W4")
else()
    set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS
        "-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion;-Wnon-virtual-dtor;-Woverloaded-virtual;-Wcast-align")
endif()

# Platform audio libraries required by miniaudio
if(WIN32)
    target_compile_definitions(audio_player PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(audio_player PRIVATE Threads::Threads m dl)
elseif(APPLE)
    find_library(COREAUDIO_LIB CoreAudio)
    find_library(AUDIOUNIT_LIB AudioUnit)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    target_link_libraries(audio_player PRIVATE ${COREAUDIO_LIB} ${AUDIOUNIT_LIB} ${COREFOUNDATION_LIB})
endif()

install(TARGETS audio_player RUNTIME DESTINATION bin)
