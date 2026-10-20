extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Json.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    std::string why(const std::string& text)
    {
        try { kuge::Json::parse(text); } catch (const kuge::JsonError& error) { return error.what(); }
        return "accepted";
    }
}

Test(json, reads_what_it_writes)
{
    const auto value = kuge::Json::parse(R"({"name": "wand", "damage": 2.5, "level": 3, "tags": ["magic", "ranged"], "nested": {"on": true, "off": false, "none": null}})");

    AssertStrEq(value["name"].string().c_str(), "wand", "a string");
    AssertEq(value["damage"].number(), 2.5, "a number");
    AssertEq(value["level"].integer(), 3, "a whole number");
    Assert(value["tags"].items().size() == 2 && value["tags"].at(1).string() == "ranged", "an array");
    Assert(value["nested"]["on"].boolean() && !value["nested"]["off"].boolean() && value["nested"]["none"].isNull(), "booleans and null");
    Assert(value.find("missing") == nullptr && value["nested"].find("on") != nullptr, "find says nothing for a missing member");
    AssertEq(value.size(), 5u, "five members");
    const std::string compact = value.dump();
    const std::string pretty = value.dump(2);

    Assert(kuge::Json::parse(compact) == value && kuge::Json::parse(pretty) == value, "compact or indented, it reads back the same");
    Assert(pretty.find("\n  \"damage\": 2.5") != std::string::npos, "indented: %s", pretty.c_str());
    AssertStrEq(kuge::Json::parse(" [ 1 , 2 ] ").dump().c_str(), "[1,2]", "spaces are ignored");
    Assert(value.dump() == kuge::Json::parse(compact).dump(), "the same content always dumps the same");
}

Test(json, strings_and_unicode)
{
    const auto value = kuge::Json::parse(R"("a\"b\\c\n\t\u00e9\u20ac\ud83c\udfae")");

    AssertStrEq(value.string().c_str(), "a\"b\\c\n\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xAE", "escapes, and \\u to UTF-8, with a pair of surrogates");
    const auto back = kuge::Json::parse(value.dump());

    Assert(back == value, "and back");
    Assert(kuge::Json("tab\tand\x01").dump() == "\"tab\\tand\\u0001\"", "a control character is written as \\u: %s", kuge::Json("tab\tand\x01").dump().c_str());
    Assert(kuge::Json::parse("\"\xC3\xA9\"").string() == "\xC3\xA9", "UTF-8 is kept as it is");
}

Test(json, errors_say_where)
{
    Assert(why("{\"a\": 1,}").find("line 1, column 9") == 0, "a trailing comma: %s", why("{\"a\": 1,}").c_str());
    Assert(why("{\n  \"a\": 1\n  \"b\": 2\n}").find("line 3, column 3: expected ','") == 0 || why("{\n  \"a\": 1\n  \"b\": 2\n}").find("line 3") == 0, "a missing comma, on the right line: %s", why("{\n  \"a\": 1\n  \"b\": 2\n}").c_str());
    Assert(why("[1, 2").find("ends too soon") != std::string::npos || why("[1, 2").find("expected") != std::string::npos, "not closed: %s", why("[1, 2").c_str());
    Assert(why("").find("ends too soon") != std::string::npos, "nothing");
    Assert(why("{\"a\": 1, \"a\": 2}").find("there twice") != std::string::npos, "a member twice: %s", why("{\"a\": 1, \"a\": 2}").c_str());
    Assert(why("[1] 2").find("after the value") != std::string::npos, "something after");
    Assert(why("01").find("does not start with 0") != std::string::npos, "01");
    Assert(why("1.").find("digits are expected") != std::string::npos && why("-").find("expected a value") != std::string::npos, "bad numbers");
    Assert(why("NaN").find("expected a value") != std::string::npos && why("Infinity").find("expected a value") != std::string::npos, "NaN and Infinity are not JSON");
    Assert(why("'a'").find("expected a value") != std::string::npos, "single quotes");
    Assert(why("\"\\x\"").find("not an escape") != std::string::npos, "an unknown escape");
    Assert(why("\"\\ud800\"").find("surrogate") != std::string::npos && why("\"\\udc00\"").find("surrogate") != std::string::npos, "lone surrogates");
    Assert(why("\"a\nb\"").find("control character") != std::string::npos, "a raw line break in a string");
    Assert(why("// c\n1").find("expected a value") != std::string::npos, "no comments");
    Assert(why(std::string(100, '[') + std::string(100, ']')).find("too deep") != std::string::npos, "too deep");
    Assert(why(std::string(60, '[') + std::string(60, ']')) == "accepted", "60 levels are fine");
}

Test(json, wrong_type_is_named)
{
    const auto value = kuge::Json::parse(R"({"n": 1.5, "s": "x", "a": []})");
    const auto message = [](const std::function<void()>& f) { try { f(); } catch (const kuge::JsonError& e) { return std::string(e.what()); } return std::string("no error"); };

    Assert(message([&] { value["s"].number(); }).find("expected a number, found a string") == 0, "string read as number");
    Assert(message([&] { value["n"].integer(); }).find("whole number") != std::string::npos, "a fraction is not an integer");
    Assert(message([&] { value["a"].string(); }).find("found an array") != std::string::npos, "an array as a string");
    Assert(message([&] { value["nothing"]; }).find("no member 'nothing'") != std::string::npos, "a missing member");
    Assert(message([&] { value["a"].at(3); }).find("no element 3") != std::string::npos, "a missing element");
    Assert(message([&] { kuge::Json(3).set("k", 1); }).find("cannot set a member of a number") != std::string::npos, "set on a number");
    kuge::Json built;

    built.set("a", 1).set("b", "two");
    built.set("list", kuge::Json::array().push(1).push("x"));
    AssertStrEq(built.dump().c_str(), R"({"a":1,"b":"two","list":[1,"x"]})", "built by hand");
}

Test(json, a_file_names_itself)
{
    const auto file = std::filesystem::temp_directory_path() / "kuge_json_test.json";

    { std::ofstream(file) << "{\"a\": }"; }
    try {
        kuge::Json::load(file);
        Assert(false, "a bad file is refused");
    } catch (const kuge::JsonError& error) {
        Assert(std::string(error.what()).find("kuge_json_test.json: line 1") != std::string::npos, "the message starts with the file: %s", error.what());
    }
    std::filesystem::remove(file);
    bool missing = false;

    try { kuge::Json::load("/nonexistent/none.json"); } catch (const kuge::JsonError&) { missing = true; }
    Assert(missing, "a missing file too");
}
