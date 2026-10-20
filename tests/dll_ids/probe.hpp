#pragma once

#include "kronkworld/Kronkworld.hpp"
#include <cstddef>

// Ticket 039: the same two types, seen from the program and from the shared library
struct ProbeResource {};
struct ProbeComponent {};
struct OtherResource {};
struct OtherComponent {};

#if defined(_WIN32)
    #define PROBE_EXPORT extern "C" __declspec(dllexport)
#else
    #define PROBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PROBE_EXPORT std::size_t moduleResourceId(void);
PROBE_EXPORT std::size_t moduleComponentId(void);
