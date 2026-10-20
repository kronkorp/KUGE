#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! A text that is not JSON (the message says the line and the column), or a value read as what it is not
    class JsonError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A JSON value: what the data of a game, a mod, a prefab and a timeline are written in
     *
     *     const kuge::Json weapon = kuge::Json::parse(text);          // @throw JsonError: "line 3, column 14: expected ':'"
     *     const double damage = weapon["damage"].number();
     *     if (const kuge::Json* sprite = weapon.find("sprite")) { ... }
     *
     *     kuge::Json out = kuge::Json::object();
     *     out.set("name", "wand").set("level", 3);
     *     file << out.dump(2);
     *
     * It is small on purpose (KUGE has no dependency for this): strict JSON (no comments, no trailing comma, no NaN), at most 64 levels,
     * numbers as doubles, objects with their keys in order (so a dump is always the same for the same content). Reading a value as
     * the wrong type throws a JsonError that says what it was.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Json
    {
        public:
            enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };
            using Array = std::vector<Json>;
            using Object = std::map<std::string, Json, std::less<>>;

            Json(void) = default;
            Json(bool value) : m_type(Type::Bool), m_bool(value) {}
            Json(int value) : m_type(Type::Number), m_number(value) {}
            Json(unsigned value) : m_type(Type::Number), m_number(value) {}
            Json(std::int64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
            Json(std::uint64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
            Json(double value) : m_type(Type::Number), m_number(value) {}
            Json(float value) : m_type(Type::Number), m_number(value) {}
            Json(const char* value) : m_type(Type::String), m_string(value) {}
            Json(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
            Json(Array value) : m_type(Type::Array), m_array(std::move(value)) {}
            Json(Object value) : m_type(Type::Object), m_object(std::move(value)) {}

            static Json array(void) { return Json(Array{}); }
            static Json object(void) { return Json(Object{}); }

            //! @throw JsonError
            static Json parse(std::string_view text);

            //! @throw JsonError with the name of the file in the message
            static Json load(const std::filesystem::path& file);

            //! Compact when indent is negative, else with that many spaces per level
            std::string dump(int indent = -1) const;

            Type type(void) const noexcept { return m_type; }
            bool isNull(void) const noexcept { return m_type == Type::Null; }
            bool isBool(void) const noexcept { return m_type == Type::Bool; }
            bool isNumber(void) const noexcept { return m_type == Type::Number; }
            bool isString(void) const noexcept { return m_type == Type::String; }
            bool isArray(void) const noexcept { return m_type == Type::Array; }
            bool isObject(void) const noexcept { return m_type == Type::Object; }
            static const char* name(Type type) noexcept;

            // -- Reading (throws a JsonError that names what the value is) ------------------------------
            bool                boolean(void) const;
            double              number(void) const;
            std::int64_t        integer(void) const;   //!< A number with no fraction
            const std::string&  string(void) const;
            const Array&        items(void) const;
            const Object&       members(void) const;

            //! The member of an object, or nothing (also for a value that is not an object)
            const Json* find(std::string_view key) const;
            //! The member of an object. @throw JsonError if there is none
            const Json& operator[](std::string_view key) const;
            //! The element of an array. @throw JsonError if it is not there
            const Json& at(std::size_t index) const;
            std::size_t size(void) const noexcept;   //!< Elements of an array, members of an object, else 0

            // -- Writing -----------------------------------------------------------------------------------
            //! Sets a member (the value becomes an object if it was null). @throw JsonError for any other type
            Json& set(std::string key, Json value);
            //! Adds an element (the value becomes an array if it was null). @throw JsonError for any other type
            Json& push(Json value);

            bool operator==(const Json& other) const;

        private:
            Type         m_type = Type::Null;
            bool         m_bool = false;
            double       m_number = 0.0;
            std::string  m_string;
            Array        m_array;
            Object       m_object;
    };

}
