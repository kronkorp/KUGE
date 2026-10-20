// Ticket 039. On Windows a DLL and a program each have their own copy of the `static` that kronkworld
// uses to number the types (ResourceManager::id<R>(), ComponentManager::id<C>()): the same type gets two
// ids, and `get<R>()` reads whatever sits at the program's id. That is why kuge.dll is not built by
// default there (KUGE_BUILD_SHARED, CMakeLists.txt). Linux merges those statics, so module.cpp is built
// with hidden symbols to behave like a DLL.
//
// This program returns 0 when both sides agree. It does not, so ctest expects it to FAIL (WILL_FAIL):
// the day kronkworld numbers its types by name, it succeeds, ctest goes red, and the guard in
// CMakeLists.txt (and the workaround of the games) can go.
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
