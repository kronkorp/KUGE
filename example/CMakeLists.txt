add_executable(kuge_example Main.cpp)
target_link_libraries(kuge_example PRIVATE kuge)
target_compile_options(kuge_example PRIVATE -Wall -Wextra)
# Find kuge.so next to the executable at runtime
set_target_properties(kuge_example PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:kuge>")
