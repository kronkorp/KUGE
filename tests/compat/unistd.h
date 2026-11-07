#pragma once
// What the tests take from <unistd.h>, for Windows (put in front of the include path there by tests/CMakeLists.txt)
#include <io.h>
#include <process.h>
#include <stdlib.h>

#define getpid _getpid

inline int setenv(const char* name, const char* value, int)
{
    return _putenv_s(name, value);
}
