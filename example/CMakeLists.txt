add_executable(kuge_example src/Main.cpp)
target_link_libraries(kuge_example PRIVATE kuge-core)
target_compile_options(kuge_example PRIVATE -Wall -Wextra)
