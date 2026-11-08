kuge_module(core DEPENDS kuge-logger kuge-kronkworld kronkpool kronk3d::static)
if(WIN32)
    target_link_libraries(kuge-core PRIVATE winmm)   # timeBeginPeriod (the frame limit of the Engine)
endif()
