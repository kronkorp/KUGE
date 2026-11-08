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

// DIAGNOSTIC (branch fix/win-diag, not to be merged): say why a test process dies
#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#include <exception>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>

inline const int kugeDiagInstalled = [] {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* p) -> LONG {
        std::fprintf(stderr, "DIAG unhandled exception code 0x%08lX at %p\n", p->ExceptionRecord->ExceptionCode, p->ExceptionRecord->ExceptionAddress);
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    });
    std::set_terminate([] {
        try {
            if (auto e = std::current_exception()) {
                std::rethrow_exception(e);
            }
            std::fprintf(stderr, "DIAG terminate with no exception\n");
        } catch (const std::exception& x) {
            std::fprintf(stderr, "DIAG terminate: %s (%s)\n", x.what(), typeid(x).name());
        } catch (...) {
            std::fprintf(stderr, "DIAG terminate: unknown exception\n");
        }
        std::fflush(stderr);
        std::abort();
    });
    _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* fn, const wchar_t* file, unsigned line, uintptr_t) {
        std::fwprintf(stderr, L"DIAG invalid parameter: %ls in %ls (%ls:%u)\n", expr ? expr : L"?", fn ? fn : L"?", file ? file : L"?", line);
        std::fflush(stderr);
    });
    return 0;
}();
