#pragma once
// What the tests take from <unistd.h>, for Windows. tests/CMakeLists.txt puts it in front of the include path and includes it
// in every test (/FI), as the tests that call setenv() do not all include <unistd.h>.
#include <io.h>
#include <process.h>
#include <stdlib.h>

#define getpid _getpid

inline int setenv(const char* name, const char* value, int)
{
    return _putenv_s(name, value);   // (an empty value removes the variable on Windows)
}

inline int unsetenv(const char* name)
{
    return _putenv_s(name, "");
}
