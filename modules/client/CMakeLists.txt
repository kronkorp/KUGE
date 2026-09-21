kuge_module(client DEPENDS kuge-core SDL2::SDL2)

# stb_image is only used by the sources of the module, not by its headers
target_include_directories(kuge-client-obj SYSTEM PRIVATE "${stb_SOURCE_DIR}")
