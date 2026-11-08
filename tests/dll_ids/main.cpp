// Ticket 039. A DLL and a program each have their own copy of a `static` of an inline function, so a
// counter in a header (what kronkworld used to number its types with) gave the same type two ids:
// ResourceManager::id<R>(), ComponentManager::id<C>(). kronkworld now takes the number from a table
// in a .cpp (name of the type -> id), which must exist once: here, in the library. Linux merges
// statics, so module.cpp is built with hidden symbols to behave like a DLL.
//
// This program returns 0 when both sides agree. It meets other types first, so that a counter of
// its own would not give the library's numbers.
#include "probe.hpp"
#include <cstdio>

int main()
{
    // The program meets other types first, so that its numbers are not the library's
    const std::size_t otherResource = kw::ResourceManager::id<OtherResource>();
    const std::size_t otherComponent = static_cast<std::size_t>(kw::ComponentManager{}.id<OtherComponent>());
    const std::size_t resource = kw::ResourceManager::id<ProbeResource>();
    const std::size_t component = static_cast<std::size_t>(kw::ComponentManager{}.id<ProbeComponent>());
    const bool sameResource = resource == moduleResourceId();
    const bool sameComponent = component == moduleComponentId();

    std::printf("resource: program %zu (after %zu), library %zu -> %s\n", resource, otherResource, moduleResourceId(),
        sameResource ? "same" : "DIFFERENT");
    std::printf("component: program %zu (after %zu), library %zu -> %s\n", component, otherComponent, moduleComponentId(),
        sameComponent ? "same" : "DIFFERENT");
    return sameResource && sameComponent ? 0 : 1;
}
