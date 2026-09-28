# Step 3: The ECS

Each scene owns a `kw::World`, from kronkworld. It is a small ECS written in C++ headers
(`vendor/kronkworld/include/kronkworld/`). Four things live in it: entities, components, resources and systems.
This step covers the first three; systems are step 4.

## An entity is a number

```cpp
// kronkworld/entity/Entity.hpp, EntityManager
using Entity = uint64_t;
using Signature = std::bitset<MAX_COMPONENTS>;          // MAX_COMPONENTS is 256

Entity create()
{
    Entity e;

    if (!m_availables.empty()) {                         // a number freed before? use it again
        e = m_availables.front();
        m_availables.pop();
    } else {
        e = m_id++;                                      // otherwise, the next one
    }
    signature(e, 0);
    return e;
}

void destroy(Entity entity)
{
    signature(entity, 0);
    m_availables.push(entity);                           // a queue: the oldest freed number goes first
}
```

An entity is only an index into `m_signatures`, a vector with one bitset per entity: bit *n* says "this entity has
the component type number *n*".

There is **no generation counter**. When an entity is destroyed, its number goes back into a queue, and a later
`create()` gives it to a brand new entity. So a `kw::Entity` that you keep can end up naming something else. Code
that must survive this either never keeps a number whose end it does not decide, or checks what the entity is
before using it. (The R-Type room kept the numbers of dead ships, and a new player could inherit one: it now finds
each ship by its owner, `shipOf()`.)

## A component type is a number too

```cpp
// kronkworld/component/Component.hpp, ComponentManager
template<typename C>
Component id(void) const
{
    static const Component id = m_id.fetch_add(1);      // one number per type, the first time it is used
    return id;
}
```

A type gets its number the first time any World in the process uses it, from a single counter (`inline static
std::atomic`). Numbers are shared by all the Worlds of the process, and there are at most 256 of them. Resources work
the same way, with a counter of their own (`MAX_RESOURCES`, also 256).

## Where components live: one sparse set per type

For each component type in use, the World has a `ComponentBox<C>`:

```cpp
// kronkworld/component/ComponentBox.hpp, ComponentBox<C>
std::vector<Entity> m_sparse;    // indexed by entity: where its value is in m_raw (-1: it has none)
std::vector<C>      m_raw;       // the values, packed
std::vector<Entity> m_reverse;   // m_reverse[i] is the entity that m_raw[i] belongs to

C& add(Entity entity, Args&&... args)
{
    grow(entity);
    auto idx = m_sparse[entity];
    if (idx == -1UL) {                                   // new: at the end
        m_raw.push_back(C{std::forward<Args>(args)...});
        m_reverse.push_back(entity);
        m_sparse[entity] = m_raw.size() - 1;
        return m_raw.back();
    }
    m_raw[idx] = C{std::forward<Args>(args)...};         // already there: replaced
    return m_raw[idx];
}

void remove(Entity entity)
{
    auto idx = m_sparse[entity];
    auto backIdx = m_reverse.size() - 1;
    auto backEntt = m_reverse[backIdx];
    if (backIdx != idx) {                                // the last value moves into the hole
        m_raw[idx] = std::move(m_raw[backIdx]);
        m_reverse[idx] = backEntt;
        m_sparse[backEntt] = idx;
    }
    m_raw.pop_back();
    m_reverse.pop_back();
    m_sparse[entity] = -1UL;
}
```

`get`, `add` and `remove` are O(1), and the values of one type are packed together in memory. The price is in
`remove`: to keep the array packed, **the last value moves into the hole**, so the order of `m_raw` changes.

The World glues the parts together:

```cpp
// kronkworld/world/World.hpp, World
template<typename C, typename ...Args>
C& add(Entity entity, Args&&... args)
{
    m_entityManager.signature(entity).set(m_componentManager.id<C>());   // the bit
    return m_componentManager.add<C>(entity, std::forward<Args>(args)...);   // the value
}

template<typename C>
bool has(Entity entity) const
{
    return m_entityManager.signature(entity).test(m_componentManager.id<C>());   // one bit test
}

void remove(Entity entity)
{
    m_componentManager.clear(entity, m_entityManager.signature(entity));  // every box its signature names
    m_entityManager.destroy(entity);                                       // the number goes to the queue
}
```

Note that `C{std::forward<Args>(args)...}` is **aggregate initialization**. That is why KUGE components are plain
structs, added with `world.add<C>(e, C{...})`.

## Views: walk the smallest box

```cpp
// kronkworld/world/View.hpp, View<C...>
View(ComponentManager& cmanager, EntityManager& emanager)
{
    (m_signature.set(cmanager.id<C>()), ...);                        // the bits every entity must have
    ... pick, among the boxes of C..., the one with the fewest entities: m_best
}

// ViewIterator::next(): skip the entities of that box that lack one of the other components
while (m_index < m_leaderEntities.size()) {
    Entity e = m_leaderEntities[m_index];
    if ((m_em.signature(e) & m_mask) == m_mask) {
        break;
    }
    m_index++;
}
```

A `view<A, B>()` walks the `m_reverse` array of whichever of the two boxes is smaller, and keeps the entities whose
signature contains both bits. Hence the rule the rest of KUGE follows: **a view's order is the packed order of some
box**. It moves when something is removed, and it can also change when another box becomes the smaller one. The
physics, the drawing, the snapshots and the R-Type rules all copy the entities into a vector and sort it before
doing anything whose result must be reproducible.

Removing entities while walking a view has the same cause: the swap moves an unvisited entity into a visited slot.
So collect the entities first, then remove them.

## Resources

A resource is one value per type and per World, kept in an array indexed by the resource type's number
(`kronkworld/ressource/RessourceManager.hpp`). `kuge::Ref<T>` is a resource that holds a pointer: it is how a
module lends an object to every scene without copying it (step 6).

## Lab

`example/lab/lab03_ecs.cpp`:

```cpp
// Lab 3: what an entity is, where components live, and why the order of a view moves
#include "kronkworld/Kronkworld.hpp"
#include <cstdio>

namespace
{
    struct Position { float x = 0.0f; float y = 0.0f; };
    struct Health   { int points = 0; };

    void show(kw::World& world, const char* when)
    {
        std::printf("%-24s view<Position> visits:", when);
        for (kw::Entity e : world.view<Position>()) {
            std::printf(" %llu", static_cast<unsigned long long>(e));
        }
        std::printf("\n");
    }
}

int main()
{
    kw::World world;
    const kw::Entity a = world.create();
    const kw::Entity b = world.create();
    const kw::Entity c = world.create();

    std::printf("a=%llu b=%llu c=%llu: an entity is a number\n",
        static_cast<unsigned long long>(a), static_cast<unsigned long long>(b), static_cast<unsigned long long>(c));
    world.add<Position>(a, Position{1.0f, 1.0f});
    world.add<Position>(b, Position{2.0f, 2.0f});
    world.add<Position>(c, Position{3.0f, 3.0f});
    world.add<Health>(b, Health{10});
    show(world, "a, b and c:");

    world.remove(a);
    show(world, "a removed:");

    const kw::Entity d = world.create();

    world.add<Position>(d, Position{4.0f, 4.0f});
    std::printf("the next entity made is %llu: a's number, given again\n", static_cast<unsigned long long>(d));
    show(world, "d made:");
    std::printf("d has Health? %s (a's components went with a)\n", world.has<Health>(d) ? "yes" : "no");

    std::printf("view<Position, Health> visits:");
    for (kw::Entity e : world.view<Position, Health>()) {
        std::printf(" %llu", static_cast<unsigned long long>(e));
    }
    std::printf(" (it walks the smaller box, Health, and checks each one's signature)\n");
}
```

What it prints (`./build/example/lab03_ecs`):

```text
a=0 b=1 c=2: an entity is a number
a, b and c:              view<Position> visits: 0 1 2
a removed:               view<Position> visits: 2 1
the next entity made is 0: a's number, given again
d made:                  view<Position> visits: 2 1 0
d has Health? no (a's components went with a)
view<Position, Health> visits: 1 (it walks the smaller box, Health, and checks each one's signature)
```

## Reading the output

- The first three entities are 0, 1 and 2: an entity is a counter.
- Removing `a` (0) moves `c` (2), the last value of the Position box, into its slot: the view now visits 2, then 1.
  Nothing was sorted, the storage just moved.
- The next entity made is 0 again, `a`'s number, taken from the queue. It has no `Health`: `remove(a)` cleared every
  box, and `create()` reset the signature. But any code that still held "0, the entity `a`" now points at `d`.
- `view<Position, Health>` visits only 1. It walked the Health box, which is the smaller one (a single entity),
  and checked each entity's signature.

Next: [Step 4: The scheduler](../04-the-scheduler/README.md).
