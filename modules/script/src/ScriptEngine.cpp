#include "ScriptEngine.hpp"
#include "Random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <optional>
#include <utility>
#include <vector>

// The functions Lua calls (the hook, the allocator, the world's methods) must not leave a C++ object alive when they raise a Lua error: Lua is C and
// raises with longjmp. So a binding is a lambda that throws, `guarded()` catches, pushes the message and raises once the lambda is gone.

struct kuge::ScriptEngine::Impl
{
    const ComponentRegistry&  registry;
    ScriptOptions             options;
    lua_State*                state = nullptr;
    kw::World*                world = nullptr;     // only during a call
    Random                    random;
    std::function<void(const std::string&)> print;

    std::size_t               used = 0;            // bytes held by Lua
    // Who holds them: 0 is the engine (the stack, the string table, the shared globals), then the scopes in load order (id = index + 1).
    // What Lua allocates is charged to `owner`, the scope that is running, and given back to the one that made it (see allocate)
    std::vector<std::size_t>  usedBy = {0};
    std::size_t               owner = 0;
    std::uint64_t             spent = 0;           // instructions of this load or call
    int                       step = 1000;         // instructions between two hooks (1 once the budget is gone: every instruction fails)
    bool                      over = false;

    // What was loaded, in order: a reload runs it again in a new state
    struct Source
    {
        std::string                                   name;
        std::string                                   code;
        std::string                                   scope;
        std::optional<std::filesystem::path>          file;
        std::pair<std::filesystem::file_time_type, std::uintmax_t> stamp;   // of the file when it was read: its date and its size
    };
    std::vector<Source>       sources;

    // The globals of each scope: a table that reads the shared globals (the libraries, `world`) and writes in itself. Kept in the registry
    // of Lua by reference; in load order
    std::vector<std::pair<std::string, int>> scopes;

    Impl(const ComponentRegistry& r, ScriptOptions o) : registry(r), options(o), random(o.seed) {}
    ~Impl()
    {
        if (state) {
            lua_close(state);
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

namespace
{
    using Impl = kuge::ScriptEngine::Impl;

    constexpr int kHookStep = 1000;
    constexpr int kMaxDepth = 8;

    Impl& self(lua_State* L)
    {
        void* data = nullptr;

        lua_getallocf(L, &data);
        return *static_cast<Impl*>(data);
    }

    // Every block has a header that says who it is charged to, so that a block freed by the garbage collector while another mod runs
    // is given back to its owner. (8 bytes: Lua needs no more than the alignment of a double, a pointer or a 64 bit integer.)
    constexpr std::size_t kHeader = 8;
    static_assert(alignof(lua_Number) <= kHeader && alignof(lua_Integer) <= kHeader && alignof(void*) <= kHeader, "Lua needs more alignment than the header keeps");

    std::size_t& ownerOf(void* real)
    {
        return *static_cast<std::size_t*>(real);
    }

    void* allocate(void* data, void* block, std::size_t oldSize, std::size_t newSize)
    {
        auto* impl = static_cast<Impl*>(data);
        const std::size_t old = block ? oldSize : 0;   // without a block, oldSize is the type of the object
        void* real = block ? static_cast<char*>(block) - kHeader : nullptr;
        const std::size_t owner = block ? ownerOf(real) : impl->owner;

        if (newSize == 0) {
            impl->used -= old;
            impl->usedBy[owner] -= old;
            std::free(real);
            return nullptr;
        }
        if (newSize > old) {
            // The total, then the mod's own share (the engine's share is only held by the total)
            if (impl->used - old + newSize > impl->options.memory
                || (owner != 0 && impl->usedBy[owner] - old + newSize > impl->options.modMemory)) {
                return nullptr;
            }
        }
        void* moved = std::realloc(real, newSize + kHeader);

        if (!moved) {
            return nullptr;
        }
        ownerOf(moved) = owner;
        impl->used = impl->used - old + newSize;
        impl->usedBy[owner] = impl->usedBy[owner] - old + newSize;
        return static_cast<char*>(moved) + kHeader;
    }

    void hook(lua_State* L, lua_Debug*)
    {
        Impl& impl = self(L);

        impl.spent += static_cast<std::uint64_t>(impl.step);
        if (impl.spent > impl.options.budget) {
            // From now on every instruction fails, in every coroutine: a script cannot catch the error with pcall and go on
            impl.over = true;
            impl.step = 1;
            lua_sethook(L, hook, LUA_MASKCOUNT, 1);
            luaL_where(L, 0);   // in a hook, level 0 is the function that was running
            lua_pushfstring(L, "the script ran over its budget of %I instructions", static_cast<lua_Integer>(impl.options.budget));
            lua_concat(L, 2);
            lua_error(L);
        }
    }

    template<typename F>
    int guarded(lua_State* L, F&& body)
    {
        int results = 0;
        bool failed = false;

        try {
            results = body();
        } catch (const std::exception& e) {
            luaL_where(L, 1);
            lua_pushstring(L, e.what());
            lua_concat(L, 2);
            failed = true;
        }
        return failed ? lua_error(L) : results;
    }

    // ---- Lua <-> Json ---------------------------------------------------------------------------------------------------

    kuge::Json toJson(lua_State* L, int index, int depth)
    {
        index = lua_absindex(L, index);
        switch (lua_type(L, index)) {
            case LUA_TNIL: return {};
            case LUA_TBOOLEAN: return kuge::Json(lua_toboolean(L, index) != 0);
            case LUA_TNUMBER:
                return lua_isinteger(L, index) ? kuge::Json(static_cast<std::int64_t>(lua_tointeger(L, index))) : kuge::Json(static_cast<double>(lua_tonumber(L, index)));
            case LUA_TSTRING: return kuge::Json(std::string(lua_tostring(L, index), lua_rawlen(L, index)));
            case LUA_TTABLE: break;
            default: throw std::runtime_error(std::format("a {} cannot be a value of the game", luaL_typename(L, index)));
        }
        if (depth > kMaxDepth) {
            throw std::runtime_error("a table is nested too deep (or contains itself)");
        }
        luaL_checkstack(L, 4, "a table to read");
        const auto length = static_cast<std::size_t>(lua_rawlen(L, index));
        std::size_t count = 0;
        bool onlyStrings = true;

        for (lua_pushnil(L); lua_next(L, index) != 0; lua_pop(L, 1)) {
            ++count;
            onlyStrings = onlyStrings && lua_type(L, -2) == LUA_TSTRING;
        }
        if (count == length && length > 0) {   // keys 1..length and nothing else
            kuge::Json out = kuge::Json::array();

            for (std::size_t i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, static_cast<lua_Integer>(i));
                out.push(toJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return out;
        }
        if (!onlyStrings) {
            throw std::runtime_error("a table of the game is a list (1, 2, 3...) or has names for its keys, not a mix");
        }
        kuge::Json out = kuge::Json::object();

        for (lua_pushnil(L); lua_next(L, index) != 0; lua_pop(L, 1)) {
            out.set(std::string(lua_tostring(L, -2), lua_rawlen(L, -2)), toJson(L, -1, depth + 1));
        }
        return out;
    }

    void pushJson(lua_State* L, const kuge::Json& json)
    {
        luaL_checkstack(L, 4, "a value to give");
        switch (json.type()) {
            case kuge::Json::Type::Null: lua_pushnil(L); break;
            case kuge::Json::Type::Bool: lua_pushboolean(L, json.boolean()); break;
            case kuge::Json::Type::Number: {
                const double number = json.number();

                if (number == std::floor(number) && std::fabs(number) < 9.0e15) {   // a whole number is an integer for the script (an entity, a count)
                    lua_pushinteger(L, static_cast<lua_Integer>(number));
                } else {
                    lua_pushnumber(L, number);
                }
                break;
            }
            case kuge::Json::Type::String: lua_pushlstring(L, json.string().data(), json.string().size()); break;
            case kuge::Json::Type::Array: {
                lua_createtable(L, static_cast<int>(json.size()), 0);
                lua_Integer i = 1;

                for (const kuge::Json& item : json.items()) {
                    pushJson(L, item);
                    lua_rawseti(L, -2, i++);
                }
                break;
            }
            case kuge::Json::Type::Object:
                lua_createtable(L, 0, static_cast<int>(json.size()));
                for (const auto& [name, member] : json.members()) {
                    pushJson(L, member);
                    lua_setfield(L, -2, name.c_str());
                }
                break;
        }
    }

    // ---- The arguments of the methods of `world` (index 1 is `world` itself: they are called with a colon) -------------------

    kw::World& worldOf(Impl& impl)
    {
        if (!impl.world) {
            throw std::runtime_error("the world can only be used inside a call (not while a script is loaded)");
        }
        return *impl.world;
    }

    kw::Entity entityArg(lua_State* L, Impl& impl, int index)
    {
        if (!lua_isinteger(L, index) || lua_tointeger(L, index) < 0 || static_cast<std::uint64_t>(lua_tointeger(L, index)) > impl.options.maxEntity) {
            throw std::runtime_error(std::format("argument #{} must be an entity: a whole number from 0 to {}", index - 1, impl.options.maxEntity));
        }
        return static_cast<kw::Entity>(lua_tointeger(L, index));
    }

    std::string componentArg(lua_State* L, Impl& impl, int index)
    {
        if (lua_type(L, index) != LUA_TSTRING) {
            throw std::runtime_error(std::format("argument #{} must be the name of a component", index - 1));
        }
        std::string name(lua_tostring(L, index), lua_rawlen(L, index));

        if (!impl.registry.byName(name)) {
            throw std::runtime_error(std::format("there is no component '{}'", name));
        }
        return name;
    }

    void check(const kuge::ComponentRegistry::Result& result)
    {
        if (!result.ok) {
            throw std::runtime_error(result.message);
        }
    }

    int worldGet(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const kw::Entity entity = entityArg(L, impl, 2);
            const std::string component = componentArg(L, impl, 3);

            if (!impl.registry.has(worldOf(impl), entity, component)) {
                lua_pushnil(L);
                return 1;
            }
            const kuge::Json fields = impl.registry.toJson(worldOf(impl), entity, component);

            if (lua_isnoneornil(L, 4)) {
                pushJson(L, fields);
                return 1;
            }
            if (lua_type(L, 4) != LUA_TSTRING) {
                throw std::runtime_error("argument #3 must be the name of a field");
            }
            const std::string field(lua_tostring(L, 4), lua_rawlen(L, 4));
            const kuge::Json* value = fields.find(field);

            if (!value) {
                throw std::runtime_error(std::format("{} has no field '{}'", component, field));
            }
            pushJson(L, *value);
            return 1;
        });
    }

    // world:set(e, "Health", "points", 5)  or  world:set(e, "Health", {points = 5, regen = 0})
    int worldSet(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const kw::Entity entity = entityArg(L, impl, 2);
            const std::string component = componentArg(L, impl, 3);
            kuge::Json fields = kuge::Json::object();

            if (lua_type(L, 4) == LUA_TTABLE) {
                fields = toJson(L, 4, 0);
            } else if (lua_type(L, 4) == LUA_TSTRING && lua_gettop(L) >= 5) {
                fields.set(std::string(lua_tostring(L, 4), lua_rawlen(L, 4)), toJson(L, 5, 0));
            } else {
                throw std::runtime_error("set(entity, component, field, value) or set(entity, component, {field = value})");
            }
            check(impl.registry.fromJson(worldOf(impl), entity, component, fields));
            return 0;
        });
    }

    int worldHas(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const kw::Entity entity = entityArg(L, impl, 2);

            lua_pushboolean(L, impl.registry.has(worldOf(impl), entity, componentArg(L, impl, 3)));
            return 1;
        });
    }

    // world:add(e, "Health")  or  world:add(e, "Health", {points = 5}): the fields that are not given keep the defaults of the component
    int worldAdd(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const kw::Entity entity = entityArg(L, impl, 2);
            const std::string component = componentArg(L, impl, 3);

            if (lua_isnoneornil(L, 4)) {
                impl.registry.add(worldOf(impl), entity, component);
            } else {
                check(impl.registry.fromJson(worldOf(impl), entity, component, toJson(L, 4, 0)));
            }
            return 0;
        });
    }

    int worldDrop(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const kw::Entity entity = entityArg(L, impl, 2);

            impl.registry.remove(worldOf(impl), entity, componentArg(L, impl, 3));
            return 0;
        });
    }

    // world:spawn{Health = {points = 5}, Enemy = {}}
    int worldSpawn(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            kw::World& world = worldOf(impl);

            if (lua_type(L, 2) != LUA_TTABLE) {
                throw std::runtime_error("spawn{Component = {field = value}, ...}: an entity with no component would be lost");
            }
            const kuge::Json components = toJson(L, 2, 0);

            if (!components.isObject() || components.size() == 0) {
                throw std::runtime_error("spawn{Component = {field = value}, ...}: an entity with no component would be lost");
            }
            const kw::Entity entity = world.create();

            if (const auto result = impl.registry.entityFromJson(world, entity, components); !result.ok) {
                world.remove(entity);
                throw std::runtime_error(result.message);
            }
            lua_pushinteger(L, static_cast<lua_Integer>(entity));
            return 1;
        });
    }

    // The entity goes with all its components. One that has none of the described ones is taken for gone already: removing an entity twice would
    // hand its number to two new ones.
    int worldRemove(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            kw::World& world = worldOf(impl);
            const kw::Entity entity = entityArg(L, impl, 2);

            if (impl.registry.componentsOf(world, entity).empty()) {
                throw std::runtime_error(std::format("the entity {} has no component: it is gone already", entity));
            }
            world.remove(entity);
            return 0;
        });
    }

    int worldComponents(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            const auto names = impl.registry.componentsOf(worldOf(impl), entityArg(L, impl, 2));

            lua_createtable(L, static_cast<int>(names.size()), 0);
            for (std::size_t i = 0; i < names.size(); ++i) {
                lua_pushlstring(L, names[i].data(), names[i].size());
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        });
    }

    // world:view{"Health", "Enemy"}: the entities that have all of them, as a list
    int worldView(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            std::vector<std::string> names;

            if (lua_type(L, 2) != LUA_TTABLE) {
                throw std::runtime_error("view{\"Component\", ...}: a list of names");
            }
            for (lua_Integer i = 1; i <= static_cast<lua_Integer>(lua_rawlen(L, 2)); ++i) {
                lua_rawgeti(L, 2, i);
                if (lua_type(L, -1) != LUA_TSTRING) {
                    throw std::runtime_error("view{\"Component\", ...}: a list of names");
                }
                names.emplace_back(lua_tostring(L, -1), lua_rawlen(L, -1));
                lua_pop(L, 1);
            }
            const auto found = impl.registry.entitiesWith(worldOf(impl), names);

            lua_createtable(L, static_cast<int>(found.size()), 0);
            for (std::size_t i = 0; i < found.size(); ++i) {
                lua_pushinteger(L, static_cast<lua_Integer>(found[i]));
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        });
    }

    // ---- Standard library, as far as it is safe and the same everywhere ---------------------------------------------------

    int mathRandom(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            lua_Integer low = 1;
            lua_Integer high = 0;

            switch (lua_gettop(L)) {
                case 0:
                    lua_pushnumber(L, static_cast<lua_Number>(impl.random.uniform()));
                    return 1;
                case 1: high = lua_tointeger(L, 1); break;
                case 2: low = lua_tointeger(L, 1); high = lua_tointeger(L, 2); break;
                default: throw std::runtime_error("wrong number of arguments to 'random'");
            }
            if (!lua_isinteger(L, 1) || !lua_isinteger(L, lua_gettop(L)) || low > high) {
                throw std::runtime_error("random(m, n): whole numbers, m <= n");
            }
            if (static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low) >= 0xFFFFFFFFull) {
                throw std::runtime_error("random(m, n): the interval is too large");
            }
            lua_pushinteger(L, low + static_cast<lua_Integer>(impl.random.below(static_cast<std::uint32_t>(high - low + 1))));
            return 1;
        });
    }

    int mathRandomSeed(lua_State* L)
    {
        return guarded(L, [&] {
            if (!lua_isinteger(L, 1)) {
                throw std::runtime_error("randomseed(n): a whole number");
            }
            self(L).random = kuge::Random(static_cast<std::uint64_t>(lua_tointeger(L, 1)));
            return 0;
        });
    }

    int print(lua_State* L)
    {
        return guarded(L, [&] {
            Impl& impl = self(L);
            std::string line;

            for (int i = 1, count = lua_gettop(L); i <= count; ++i) {
                std::size_t length = 0;
                const char* text = luaL_tolstring(L, i, &length);

                line += i > 1 ? "\t" : "";
                line.append(text, length);
                lua_pop(L, 1);
            }
            if (impl.print) {
                impl.print(line);
            }
            return 0;
        });
    }

    void openLibrary(lua_State* L, const char* name, lua_CFunction open)
    {
        luaL_requiref(L, name, open, 1);
        lua_pop(L, 1);
    }

    void removeGlobals(lua_State* L, std::initializer_list<const char*> names)
    {
        for (const char* name : names) {
            lua_pushnil(L);
            lua_setglobal(L, name);
        }
    }

    // Runs what is on the stack (a function and its arguments) with a fresh budget. @return "" or the message of the error
    std::string run(Impl& impl, int arguments)
    {
        impl.spent = 0;
        impl.over = false;
        impl.step = kHookStep;
        lua_sethook(impl.state, hook, LUA_MASKCOUNT, kHookStep);
        const int status = lua_pcall(impl.state, arguments, 0, 0);

        lua_sethook(impl.state, nullptr, 0, 0);
        if (status == LUA_OK) {
            return {};
        }
        std::string why = lua_type(impl.state, -1) == LUA_TSTRING ? lua_tostring(impl.state, -1) : std::format("(the error is a {})", luaL_typename(impl.state, -1));

        lua_pop(impl.state, 1);
        return why;
    }
    std::unique_ptr<Impl> makeImpl(const kuge::ComponentRegistry& registry, const kuge::ScriptOptions& options)
    {
        auto impl = std::make_unique<Impl>(registry, options);

#if LUA_VERSION_NUM >= 505
        impl->state = lua_newstate(allocate, impl.get(), 0);   // a fixed seed: the order of `pairs` is the same in every run
#else
        impl->state = lua_newstate(allocate, impl.get());
#endif
        if (!impl->state) {
            throw kuge::ScriptError("cannot create a Lua state");
        }
        lua_State* L = impl->state;

        openLibrary(L, "_G", luaopen_base);
        openLibrary(L, LUA_TABLIBNAME, luaopen_table);
        openLibrary(L, LUA_STRLIBNAME, luaopen_string);
        openLibrary(L, LUA_MATHLIBNAME, luaopen_math);
        openLibrary(L, LUA_UTF8LIBNAME, luaopen_utf8);
        openLibrary(L, LUA_COLIBNAME, luaopen_coroutine);
        removeGlobals(L, {"dofile", "loadfile", "load", "collectgarbage"});
        lua_pushcfunction(L, print);
        lua_setglobal(L, "print");

        lua_getglobal(L, "math");
        lua_pushcfunction(L, mathRandom);
        lua_setfield(L, -2, "random");
        lua_pushcfunction(L, mathRandomSeed);
        lua_setfield(L, -2, "randomseed");
        lua_pop(L, 1);

        static const luaL_Reg methods[] = {
            {"get", worldGet}, {"set", worldSet}, {"has", worldHas}, {"add", worldAdd}, {"drop", worldDrop},
            {"spawn", worldSpawn}, {"remove", worldRemove}, {"view", worldView}, {"components", worldComponents}, {nullptr, nullptr},
        };

        lua_newtable(L);
        luaL_setfuncs(L, methods, 0);
        lua_setglobal(L, "world");

        // The metatable of strings reads the engine's own `string` table: a script must not get it (`getmetatable("").__index`)
        lua_pushliteral(L, "");
        lua_getmetatable(L, -1);
        lua_pushliteral(L, "the metatable of strings is not for scripts");
        lua_setfield(L, -2, "__metatable");
        lua_pop(L, 2);
        return impl;
    }

    // Pushes a shallow copy of the table at `index`. (The libraries and `world` hold functions and strings only: nothing is shared)
    void copyTable(lua_State* L, int index)
    {
        index = lua_absindex(L, index);
        lua_newtable(L);
        lua_pushnil(L);
        while (lua_next(L, index)) {        // copy, key, value
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, -5);
            lua_pop(L, 1);
        }
    }

    // Pushes a new table of globals for a scope: a copy of the shared globals, and the libraries and `world` are copies too, so that
    // `math.floor = nil` or `world.get = nil` in one mod stays in that mod. No metatable (the one that read the shared globals was a door
    // to them: `getmetatable(_G).__index`). Runs under lua_pcall (a Lua function that cannot raise but for memory)
    int newScope(lua_State* L)
    {
        lua_newtable(L);                    // 1: the scope's globals
        lua_pushglobaltable(L);             // 2: the shared ones
        lua_pushnil(L);
        while (lua_next(L, 2)) {            // key, value
            if (lua_rawequal(L, -1, 2)) {   // `_G`: the scope gets its own below
                lua_pop(L, 1);
                continue;
            }
            lua_pushvalue(L, -2);
            if (lua_istable(L, -2)) {
                copyTable(L, -2);
            } else {
                lua_pushvalue(L, -2);
            }
            lua_rawset(L, 1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "_G");          // `_G` is the scope's own table: `_G.x = 1` does not leak into the others
        return 1;
    }

    // Whatever Lua allocates while this lives is charged to the scope with that index (+ 1: 0 is the engine)
    struct Owner
    {
        Impl&       impl;
        std::size_t before;

        Owner(Impl& i, std::size_t scopeIndex) : impl(i), before(i.owner) { impl.owner = scopeIndex + 1; }
        ~Owner() { impl.owner = before; }
        Owner(const Owner&) = delete;
        Owner& operator=(const Owner&) = delete;
    };

    // The index of a scope, made on first use (its globals are charged to it)
    std::size_t scopeIndex(Impl& impl, const std::string& scope)
    {
        for (std::size_t i = 0; i < impl.scopes.size(); ++i) {
            if (impl.scopes[i].first == scope) {
                return i;
            }
        }
        lua_State* L = impl.state;
        const std::size_t index = impl.scopes.size();

        impl.usedBy.push_back(0);
        lua_pushcfunction(L, newScope);
        int status = LUA_ERRMEM;
        {
            const Owner owner(impl, index);

            status = lua_pcall(L, 0, 1, 0);
        }
        if (status != LUA_OK) {
            const std::string why = lua_tostring(L, -1);

            lua_pop(L, 1);
            impl.usedBy.pop_back();
            throw kuge::ScriptError(why);
        }
        impl.scopes.emplace_back(scope, luaL_ref(L, LUA_REGISTRYINDEX));
        return index;
    }

    // Runs a chunk in this state, with the globals of its scope. @throw ScriptError
    void loadInto(Impl& impl, std::string_view code, const std::string& name, const std::string& scope)
    {
        lua_State* L = impl.state;
        const std::size_t index = scopeIndex(impl, scope);
        const Owner owner(impl, index);

        // "t": a text chunk, never bytecode (which can break the virtual machine)
        if (luaL_loadbufferx(L, code.data(), code.size(), ("@" + name).c_str(), "t") != LUA_OK) {
            const std::string why = lua_tostring(L, -1);

            lua_pop(L, 1);
            throw kuge::ScriptError(why);
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, impl.scopes[index].second);
        lua_setupvalue(L, -2, 1);           // the first upvalue of a chunk is its _ENV: its globals are the scope's
        if (const std::string why = run(impl, 0); !why.empty()) {
            throw kuge::ScriptError(why);
        }
    }

    // Pushes the function of a scope, or nothing: true if it was there. (rawget: only what the scope itself defined)
    bool pushFunction(Impl& impl, const std::pair<std::string, int>& scope, const std::string& function)
    {
        lua_State* L = impl.state;

        lua_rawgeti(L, LUA_REGISTRYINDEX, scope.second);
        lua_pushlstring(L, function.data(), function.size());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        if (lua_isfunction(L, -1)) {
            return true;
        }
        lua_pop(L, 1);
        return false;
    }

    std::string readFile(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);

        if (!in) {
            throw kuge::ScriptError(std::format("{}: cannot read the script", file.generic_string()));
        }
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::pair<std::filesystem::file_time_type, std::uintmax_t> stampOf(const std::filesystem::path& file)
    {
        std::error_code ignored;
        const auto time = std::filesystem::last_write_time(file, ignored);
        const auto size = std::filesystem::file_size(file, ignored);

        return {time, size};
    }
}

kuge::ScriptEngine::ScriptEngine(const ComponentRegistry& registry, ScriptOptions options) : m_impl(makeImpl(registry, options))
{
}

kuge::ScriptEngine::~ScriptEngine() = default;

void kuge::ScriptEngine::loadString(std::string_view code, const std::string& name, const std::string& scope)
{
    loadInto(*m_impl, code, name, scope);
    m_impl->sources.push_back({name, std::string(code), scope, std::nullopt, {}});
}

void kuge::ScriptEngine::load(const std::filesystem::path& file, const std::string& scope)
{
    std::string code = readFile(file);
    const std::string name = file.generic_string();

    loadInto(*m_impl, code, name, scope);
    m_impl->sources.push_back({name, std::move(code), scope, file, stampOf(file)});
}

kuge::ScriptEngine::Reload kuge::ScriptEngine::reloadChanged(void)
{
    auto& sources = m_impl->sources;
    const bool changed = std::any_of(sources.begin(), sources.end(), [](const Impl::Source& s) { return s.file && stampOf(*s.file) != s.stamp; });

    if (!changed) {
        return {};
    }
    // Everything runs again in a new state; the old one is kept if anything fails (and the new dates are kept too, so the error is
    // said once, and the files are tried again when one changes)
    auto next = makeImpl(m_impl->registry, m_impl->options);
    std::vector<Impl::Source> fresh = sources;
    Reload report;

    next->print = m_impl->print;
    next->random = m_impl->random;
    try {
        for (Impl::Source& source : fresh) {
            if (source.file) {
                source.code = readFile(*source.file);
                source.stamp = stampOf(*source.file);
            }
            loadInto(*next, source.code, source.name, source.scope);
        }
    } catch (const ScriptError& e) {
        for (Impl::Source& source : sources) {
            if (source.file) {
                source.stamp = stampOf(*source.file);
            }
        }
        report.error = e.what();
        return report;
    }
    next->sources = std::move(fresh);
    m_impl = std::move(next);
    report.reloaded = true;
    return report;
}

std::size_t kuge::ScriptEngine::memoryUsed(const std::string& scope) const
{
    for (std::size_t i = 0; i < m_impl->scopes.size(); ++i) {
        if (m_impl->scopes[i].first == scope) {
            return m_impl->usedBy[i + 1];
        }
    }
    return 0;
}

std::vector<std::string> kuge::ScriptEngine::scopes(const std::string& function) const
{
    std::vector<std::string> found;

    for (const auto& scope : m_impl->scopes) {
        if (pushFunction(*m_impl, scope, function)) {
            lua_pop(m_impl->state, 1);
            found.push_back(scope.first);
        }
    }
    return found;
}

bool kuge::ScriptEngine::has(const std::string& function) const
{
    return !scopes(function).empty();
}

kuge::ScriptEngine::Result kuge::ScriptEngine::call(kw::World& world, const std::string& scope, const std::string& function, std::span<const double> args)
{
    lua_State* L = m_impl->state;
    const auto found = std::find_if(m_impl->scopes.begin(), m_impl->scopes.end(), [&](const auto& s) { return s.first == scope; });

    if (found == m_impl->scopes.end() || !pushFunction(*m_impl, *found, function)) {
        return {false, std::format("there is no function '{}'", function)};
    }
    for (const double arg : args) {
        lua_pushnumber(L, arg);
    }
    m_impl->world = &world;
    std::string why;
    {
        const Owner owner(*m_impl, static_cast<std::size_t>(found - m_impl->scopes.begin()));

        why = run(*m_impl, static_cast<int>(args.size()));
    }
    m_impl->world = nullptr;
    return {why.empty(), std::move(why)};
}

kuge::ScriptEngine::Result kuge::ScriptEngine::call(kw::World& world, const std::string& function, std::span<const double> args)
{
    const std::vector<std::string> names = scopes(function);
    Result total;

    if (names.empty()) {
        return {false, std::format("there is no function '{}'", function)};
    }
    for (const std::string& name : names) {
        if (Result one = call(world, name, function, args); !one.ok) {
            total.message += (total.ok ? "" : "\n") + one.message;
            total.ok = false;
        }
    }
    return total;
}

void kuge::ScriptEngine::seed(std::uint64_t seed)
{
    m_impl->random = Random(seed);
}

void kuge::ScriptEngine::onPrint(std::function<void(const std::string&)> sink)
{
    m_impl->print = std::move(sink);
}
