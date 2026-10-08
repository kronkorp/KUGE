kuge_module(client DEPENDS kuge-core ${KUGE_SDL_TARGETS})

# stb_image is only used by the sources of the module, not by its headers
target_include_directories(kuge-client-obj SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
