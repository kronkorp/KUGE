extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Serializer.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Color : std::uint8_t { Red = 1, Blue = 200 };
    enum Wide : std::int32_t { Negative = -5 };

    template<typename F>
    bool throwsSerializerError(F&& action)
    {
        try {
            action();
        } catch (const kuge::SerializerError&) {
            return true;
        }
        return false;
    }

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path()
            / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }
}

Test(serializer, scalars_round_trip)
{
    kuge::ByteWriter out;

    out.write<std::int8_t>(-128);
    out.write<std::uint8_t>(255);
    out.write<std::int16_t>(-32768);
    out.write<std::uint32_t>(4000000000u);
    out.write<std::int64_t>(std::numeric_limits<std::int64_t>::min());
    out.write<std::uint64_t>(std::numeric_limits<std::uint64_t>::max());
    out.write(3.5f);
    out.write(-2.25);
    out.write(true);
    out.write(false);
    out.write(Color::Blue);
    out.write(Negative);
    out.write('x');

    kuge::ByteReader in(out.bytes());

    AssertEq(in.read<std::int8_t>(), -128, "int8");
    AssertEq(in.read<std::uint8_t>(), 255, "uint8");
    AssertEq(in.read<std::int16_t>(), -32768, "int16");
    AssertEq(in.read<std::uint32_t>(), 4000000000u, "uint32");
    AssertEq(in.read<std::int64_t>(), std::numeric_limits<std::int64_t>::min(), "int64 min");
    AssertEq(in.read<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max(), "uint64 max");
    AssertEq(in.read<float>(), 3.5f, "float");
    AssertEq(in.read<double>(), -2.25, "double");
    Assert(in.read<bool>(), "true");
    AssertEq(in.read<bool>(), false, "false");
    AssertEq(in.read<Color>(), Color::Blue, "enum class");
    AssertEq(in.read<Wide>(), Negative, "negative enum");
    AssertEq(in.read<char>(), 'x', "char");
    Assert(in.atEnd(), "everything was read");
}

Test(serializer, little_endian_layout)
{
    kuge::ByteWriter out;

    out.write<std::uint32_t>(0x01020304u);
    out.write<std::uint16_t>(0x0A0B);
    out.write(1.0);

    const std::uint8_t expected[] = {0x04, 0x03, 0x02, 0x01, 0x0B, 0x0A,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};

    AssertEq(out.size(), sizeof(expected), "14 bytes, got %zu", out.size());
    AssertEq(std::memcmp(out.bytes().data(), expected, sizeof(expected)), 0,
        "the layout is the same on every machine");
    AssertEq(kuge::fourcc('A', 'B', 'C', 'D'), 0x44434241u, "fourcc");
}

Test(serializer, floats_keep_their_bits)
{
    const double values[] = {-0.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min()};
    kuge::ByteWriter out;

    for (double value : values) {
        out.write(value);
    }
    kuge::ByteReader in(out.bytes());
    for (double value : values) {
        const double back = in.read<double>();

        AssertEq(std::bit_cast<std::uint64_t>(back), std::bit_cast<std::uint64_t>(value),
            "the bits of %f must not change", value);
    }
}

Test(serializer, strings_and_vectors)
{
    kuge::ByteWriter out;
    const std::string binary("a\0b", 3);

    out.writeString("");
    out.writeString("h\xC3\xA9llo");
    out.writeString(binary);
    out.writeVector(std::vector<std::int32_t>{1, -2, 3});
    out.writeVector(std::vector<double>{});
    out.writeVector(std::vector<std::uint8_t>{7});

    kuge::ByteReader in(out.bytes());

    AssertEq(in.readString().size(), 0, "empty string");
    AssertStrEq(in.readString().c_str(), "h\xC3\xA9llo", "utf-8 is kept as is");
    Assert(in.readString() == binary, "a string can hold a NUL");
    const auto ints = in.readVector<std::int32_t>();
    AssertEq(ints.size(), 3, "3 ints");
    AssertEq(ints[1], -2, "second int");
    AssertEq(in.readVector<double>().size(), 0, "empty vector");
    AssertEq(in.readVector<std::uint8_t>().at(0), 7, "bytes");
    Assert(in.atEnd(), "everything was read");
}

Test(serializer, reading_too_much_throws)
{
    const std::uint8_t data[] = {1, 2, 3};
    kuge::ByteReader in(data);

    Assert(throwsSerializerError([&] { in.read<std::uint32_t>(); }), "4 bytes out of 3");
    AssertEq(in.position(), 0, "a failed read moves nothing");
    AssertEq(in.read<std::uint16_t>(), 0x0201, "the data is still readable");
    Assert(throwsSerializerError([&] { in.readBytes(2); }), "2 bytes out of 1");
    AssertEq(in.readBytes(1).size(), 1, "the last one");
}

Test(serializer, lying_sizes_throw)
{
    // A count of 1000 (little-endian) with 3 bytes after it
    const std::uint8_t few[] = {0xE8, 0x03, 0x00, 0x00, 1, 2, 3};
    // The biggest count possible: must be refused, not allocated
    const std::uint8_t huge[] = {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4};

    Assert(throwsSerializerError([&] { kuge::ByteReader(few).readString(); }), "string");
    Assert(throwsSerializerError([&] { kuge::ByteReader(few).readVector<std::uint32_t>(); }), "vector");
    Assert(throwsSerializerError([&] { kuge::ByteReader(huge).readVector<double>(); }), "huge vector");
    Assert(throwsSerializerError([&] { kuge::ByteReader(huge).readString(); }), "huge string");
}

Test(serializer, bad_bool_throws)
{
    const std::uint8_t data[] = {2};

    Assert(throwsSerializerError([&] { kuge::ByteReader(data).read<bool>(); }), "2 is not a bool");
}

Test(serializer, header_identifies_files)
{
    const auto save = kuge::fourcc('K', 'S', 'A', 'V');
    kuge::ByteWriter out;

    out.writeHeader(save, 3);
    out.write<std::uint8_t>(9);

    kuge::ByteReader in(out.bytes());
    AssertEq(in.readHeader(save), 3, "the version comes back");
    AssertEq(in.read<std::uint8_t>(), 9, "and the data follows");
    AssertEq(throwsSerializerError([&] { kuge::ByteReader(out.bytes()).readHeader(kuge::fourcc('N', 'O', 'P', 'E')); }),
        true, "another kind of file is refused");
}

Test(serializer, files_are_replaced_whole)
{
    const auto path = tempPath("serializer.bin");
    const std::uint8_t first[] = {1, 2, 3};
    const std::uint8_t second[] = {9};

    kuge::writeFile(path, first);
    AssertEq(kuge::readFile(path).size(), 3, "first version");
    kuge::writeFile(path, second);
    AssertEq(kuge::readFile(path).size(), 1, "replaced");
    AssertEq(kuge::readFile(path)[0], 9, "with the new content");
    AssertEq(std::filesystem::exists(path.string() + ".tmp"), false, "no temporary file left");
    std::filesystem::remove(path);

    Assert(throwsSerializerError([&] { kuge::readFile(path); }), "missing file");
    const auto nowhere = tempPath("no_such_dir") / "file.bin";
    Assert(throwsSerializerError([&] { kuge::writeFile(nowhere, first); }), "missing directory");
}
