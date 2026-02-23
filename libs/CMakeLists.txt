# Layer 0: no internal deps
add_subdirectory(binutil)
add_subdirectory(lzss)
add_subdirectory(lzo)
add_subdirectory(armapath)
add_subdirectory(tga)
add_subdirectory(surface)
add_subdirectory(roadobj)
add_subdirectory(tb)
add_subdirectory(heightpipe)

# Layer 1: depends on Layer 0
add_subdirectory(pbo)
add_subdirectory(config)
add_subdirectory(ogg)
add_subdirectory(wss)
add_subdirectory(shp)
add_subdirectory(objcat)

# Layer 2: depends on 0+1
add_subdirectory(paa)
add_subdirectory(wrp)
add_subdirectory(p3d)
add_subdirectory(console_unicode)
add_subdirectory(rvmat)

# Layer 3: depends on 0+1+2
add_subdirectory(roadnet)
add_subdirectory(forestshape)
add_subdirectory(pboindex)
