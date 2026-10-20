# Ticket 039: kronkworld numbers its types with a counter per binary (see main.cpp). A plain program
# and a shared library, no kronklab: Windows, where it really happens, runs it too.
add_library(kuge_dll_ids_module SHARED module.cpp)
target_link_libraries(kuge_dll_ids_module PRIVATE kuge-kronkworld)
# Linux shares the statics of a program and of its libraries; hidden, they are the library's own, like a DLL's
set_target_properties(kuge_dll_ids_module PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

add_executable(kuge_dll_ids main.cpp)
target_link_libraries(kuge_dll_ids PRIVATE kuge-kronkworld kuge_dll_ids_module)
add_test(NAME dll_ids_differ COMMAND kuge_dll_ids)
# Expected to fail until kronkworld gives the same id in every binary (ticket 039)
set_tests_properties(dll_ids_differ PROPERTIES WILL_FAIL TRUE)
