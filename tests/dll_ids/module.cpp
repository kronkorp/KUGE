// The shared library: it numbers the types in its own order, as kuge.dll does
#include "probe.hpp"

std::size_t moduleResourceId(void) { return kw::ResourceManager::id<ProbeResource>(); }
std::size_t moduleComponentId(void) { return static_cast<std::size_t>(kw::ComponentManager{}.id<ProbeComponent>()); }
