# Ticket 039: a program and a shared library must give the same id to a type (kronkworld numbers its
# types by name, from one table, see main.cpp). A plain program and a shared library, no kronklab:
# Windows, where the numbers used to differ, runs it too.
#
# The library is built like kuge.dll: kronkworld's objects are in it (and exported on Windows), and
# the program links the library BEFORE the static kronkworld, so its calls to the table land in the
# library's copy.
add_library(kuge_dll_ids_module SHARED module.cpp $<TARGET_OBJECTS:kuge-kronkworld-obj>)
target_link_libraries(kuge_dll_ids_module PRIVATE kuge-kronkworld)
# Linux shares the statics of a program and of its libraries; hidden, they are the library's own, like a DLL's
set_target_properties(kuge_dll_ids_module PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_executable(kuge_dll_ids main.cpp)
target_link_libraries(kuge_dll_ids PRIVATE kuge_dll_ids_module kuge-kronkworld)
add_test(NAME dll_ids_agree COMMAND kuge_dll_ids)
