extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Utf8.hpp"
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

Test(utf8, valid_and_not)
{
    Assert(kuge::utf8Valid("") && kuge::utf8Valid("plain ascii"), "ascii");
    Assert(kuge::utf8Valid("\xC3\xA9") && kuge::utf8Valid("\xE2\x82\xAC") && kuge::utf8Valid("\xF0\x9F\x8E\xAE"), "2, 3 and 4 bytes");
    Assert(kuge::utf8Valid("\xF4\x8F\xBF\xBF"), "the last code point, U+10FFFF");
    Assert(!kuge::utf8Valid("\xF4\x90\x80\x80"), "one above it");
    Assert(!kuge::utf8Valid("\xC0\xAF") && !kuge::utf8Valid("\xE0\x80\xAF") && !kuge::utf8Valid("\xF0\x80\x80\xAF"), "overlong forms of '/'");
    Assert(!kuge::utf8Valid("\xED\xA0\x80") && !kuge::utf8Valid("\xED\xBF\xBF"), "surrogates");
    Assert(kuge::utf8Valid("\xED\x9F\xBF") && kuge::utf8Valid("\xEE\x80\x80"), "(the code points on each side of them are fine)");
    Assert(!kuge::utf8Valid("\xC3") && !kuge::utf8Valid("\xE2\x82") && !kuge::utf8Valid("a\xF0\x9F\x8E"), "cut sequences");
    Assert(!kuge::utf8Valid("\xFF") && !kuge::utf8Valid("\xF5\x80\x80\x80") && !kuge::utf8Valid("\xC1\xBF"), "bytes that start nothing");
    Assert(!kuge::utf8Valid("\x80") && !kuge::utf8Valid("a\xBF"), "a continuation byte alone");
    Assert(!kuge::utf8Valid("\xC3\x28") && !kuge::utf8Valid("\xE2\x28\xA1"), "a sequence that is broken inside");
    Assert(kuge::utf8Valid(std::string("a\0b", 3)), "a zero byte is valid UTF-8 (the chat refuses it as a control)");
}

Test(utf8, length_and_trim)
{
    AssertEq(kuge::utf8Length(""), 0u, "empty");
    AssertEq(kuge::utf8Length("abc"), 3u, "ascii");
    AssertEq(kuge::utf8Length("\xC3\xA9t\xC3\xA9"), 3u, "bytes are not characters: 3 for 5 bytes");
    AssertEq(kuge::utf8Length("\xF0\x9F\x8E\xAE!"), 2u, "an emoji is one");
    Assert(kuge::trimSpaces("  hi \n") == "hi" && kuge::trimSpaces("hi") == "hi" && kuge::trimSpaces(" \t\r\n").empty(), "trim");
    Assert(kuge::trimSpaces(" a b ") == "a b", "inside is kept");
}
