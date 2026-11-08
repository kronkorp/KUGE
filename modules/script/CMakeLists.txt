kuge_module(script DEPENDS kuge-core ${LUA_LIBRARIES})
foreach(target kuge-script-obj kuge-script)
    target_include_directories(${target} PUBLIC ${LUA_INCLUDE_DIR})
endforeach()
