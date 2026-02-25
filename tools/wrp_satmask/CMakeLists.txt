add_executable(wrp_satmask
    main.cpp
    asset_provider.cpp
    mapinfo_tiles.cpp
    mosaic.cpp
    png_stream_writer.cpp
)
target_link_libraries(wrp_satmask PRIVATE
    armatools::armapath
    armatools::lzss
    armatools::paa
    armatools::pbo
    armatools::pboindex
    armatools::rvmat
    armatools::wrp
    stb_image_write
    PNG::PNG
)
armatools_set_warnings(wrp_satmask)
install(TARGETS wrp_satmask RUNTIME DESTINATION bin)
