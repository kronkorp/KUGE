kuge_module(render3d DEPENDS kuge-client kronk3d::static)

# kronk3d's Log.hpp (setLogSink, which its README documents) is not among its public headers: the module
# takes it from its sources, to send kronk3d's messages to KUGE's logger (see Render3DModule.cpp)
target_include_directories(kuge-render3d-obj PRIVATE "${PROJECT_SOURCE_DIR}/vendor/kronk3d/modules/core/src/utils")
