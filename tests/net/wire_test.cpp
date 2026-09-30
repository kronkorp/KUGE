extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Matchmaking.hpp"
#include "Wire.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Mode : std::uint8_t { Idle = 3, Busy = 9 };

    struct Point
    {
        KUGE_MESSAGE(Point, x, y)
        std::int32_t x = 0;
        std::int32_t y = 0;
    };

    struct Everything
    {
        KUGE_MESSAGE(Everything, small, wide, negative, single, precise, flag, mode, name, list, pair, maybe, nothing, vec, rect, point, points)
        std::uint8_t              small = 0;
        std::uint16_t             wide = 0;
        std::int64_t              negative = 0;
        float                     single = 0;
        double                    precise = 0;
        bool                      flag = false;
        Mode                      mode = Mode::Idle;
        std::string               name;
        std::vector<std::uint32_t> list;
        std::array<std::int16_t, 3> pair{};
        std::optional<std::string> maybe;
        std::optional<std::string> nothing;
        kuge::Vec2                vec;
        kuge::Rect                rect;
        Point                     point;
        std::vector<Point>        points;
    };

    struct Empty { KUGE_MESSAGE(Empty) };

    template<typename T>
    std::vector<std::uint8_t> written(const T& value)
    {
        kuge::ByteWriter out;

        kuge::net::encode(out, value);
        return out.bytes();
    }
}

Test(wire, ids_come_from_names)
{
    Assert(Point::kugeMessageId == kuge::net::hashName("Point"), "the id is the hash of the name");
    Assert(Point::kugeMessageId != Empty::kugeMessageId, "two names, two ids");
    AssertEq(kuge::net::hashName(""), 2166136261u, "FNV-1a of nothing");
    AssertEq(kuge::net::hashName("a"), 0xE40C292Cu, "FNV-1a of 'a' (the reference value): the id is the same on every machine");
    AssertStrEq(Point::kugeMessageName, "Point", "and the name is kept");
}

Test(wire, everything_round_trips)
{
    Everything in;

    in.small = 200; in.wide = 65000; in.negative = -1234567890123LL; in.single = 1.5f; in.precise = -2.25;
    in.flag = true; in.mode = Mode::Busy; in.name = "héllo"; in.list = {1, 2, 4000000000u};
    in.pair = {-1, 0, 32767}; in.maybe = "there"; in.vec = {3.0f, -4.0f}; in.rect = {1, 2, 3, 4};
    in.point = {7, -8}; in.points = {{1, 2}, {3, 4}};
    const auto bytes = written(in);
    kuge::ByteReader reader(bytes);
    Everything out;

    kuge::net::decode(reader, out);
    Assert(reader.atEnd(), "everything was read");
    AssertEq(out.small, 200, "u8");
    AssertEq(out.wide, 65000, "u16");
    Assert(out.negative == -1234567890123LL, "i64");
    Assert(out.single == 1.5f && out.precise == -2.25, "floats");
    Assert(out.flag && out.mode == Mode::Busy, "bool and enum");
    AssertStrEq(out.name.c_str(), "héllo", "a string");
    Assert(out.list == in.list && out.pair == in.pair, "vector and array");
    Assert(out.maybe.has_value() && *out.maybe == "there" && !out.nothing.has_value(), "optionals");
    Assert(out.vec == in.vec && out.rect.w == 3.0f && out.rect.h == 4.0f, "Vec2 and Rect");
    Assert(out.point.x == 7 && out.point.y == -8, "a message inside a message");
    Assert(out.points.size() == 2 && out.points[1].y == 4, "a vector of them");
}

Test(wire, same_bytes_everywhere)
{
    Point point{0x01020304, -1};
    const auto bytes = written(point);

    // Little-endian, the exact size: what the format promises on every machine
    const std::vector<std::uint8_t> expected = {0x04, 0x03, 0x02, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};

    Assert(bytes == expected, "a Point is two 4-byte little-endian numbers");
    Assert(written(Empty{}).empty(), "no fields: no bytes");
}

Test(wire, truncated_data_throws)
{
    Everything in;
    in.name = "some text";
    in.list = {1, 2, 3};
    const auto bytes = written(in);

    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        kuge::ByteReader reader(std::span<const std::uint8_t>(bytes.data(), cut));
        Everything out;
        bool threw = false;

        try { kuge::net::decode(reader, out); } catch (const kuge::SerializerError&) { threw = true; }
        Assert(threw, "cut at %zu of %zu: an error, not garbage", cut, bytes.size());
    }
}

Test(wire, a_lying_length_is_refused)
{
    // A vector that says it has 4 billion elements, in 8 bytes
    kuge::ByteWriter out;

    out.write<std::uint32_t>(4000000000u);
    out.write<std::uint32_t>(1);
    kuge::ByteReader reader(out.bytes());
    std::vector<std::uint32_t> list;
    bool threw = false;

    try { kuge::net::decode(reader, list); } catch (const kuge::SerializerError&) { threw = true; }
    Assert(threw, "refused before allocating");
    Assert(list.empty(), "and nothing was made");
}

Test(wire, a_bad_bool_is_refused)
{
    const std::vector<std::uint8_t> bytes = {7};
    kuge::ByteReader reader(bytes);
    bool value = false;
    bool threw = false;

    try { kuge::net::decode(reader, value); } catch (const kuge::SerializerError&) { threw = true; }
    Assert(threw, "a bool is 0 or 1");
}

// -- The messages that name, list and choose rooms ----------------------------------------------------
Test(wire, room_messages_round_trip)
{
    using namespace kuge::net;

    RoomList list;

    list.total = 3;
    list.rooms.push_back(RoomInfo{7, "duel", "Les copains", 1, 2});
    list.rooms.push_back(RoomInfo{9, "duel", "Caf\xC3\xA9 \xF0\x9F\x8E\xAE", 2, 2});
    const auto bytes = written(list);
    kuge::ByteReader reader(bytes);
    RoomList back;

    decode(reader, back);
    AssertEq(back.total, 3, "the total");
    AssertEq(back.rooms.size(), 2, "and the two rooms");
    AssertEq(back.rooms[0].roomId, 7, "the id");
    AssertStrEq(back.rooms[0].name.c_str(), "Les copains", "the name");
    AssertStrEq(back.rooms[1].name.c_str(), "Caf\xC3\xA9 \xF0\x9F\x8E\xAE", "a name with accents and an emoji");
    AssertEq(back.rooms[1].players, 2, "how many are in");

    CreateRoom create{"duel", "Secret", "Ana", true};
    const auto createBytes = written(create);
    kuge::ByteReader createReader(createBytes);
    CreateRoom createBack;

    decode(createReader, createBack);
    Assert(createBack.isPrivate && createBack.roomName == "Secret" && createBack.playerName == "Ana", "CreateRoom comes back whole");
    AssertEq(createBack.protocol, MATCHMAKING_VERSION, "with the version of the protocol");

    JoinNamedRoom join{12, "Secret", "Ben"};
    const auto joinBytes = written(join);
    kuge::ByteReader joinReader(joinBytes);
    JoinNamedRoom joinBack;

    decode(joinReader, joinBack);
    Assert(joinBack.roomId == 12 && joinBack.roomName == "Secret" && joinBack.playerName == "Ben", "so does JoinNamedRoom");
    Assert(ListRooms::kugeMessageId != RoomList::kugeMessageId && CreateRoom::kugeMessageId != JoinNamedRoom::kugeMessageId, "each has its own id");
}

Test(wire, a_room_name)
{
    using kuge::net::validRoomName;
    using kuge::net::trimRoomName;

    Assert(validRoomName("Les copains"), "a name");
    Assert(validRoomName("a"), "one letter is enough");
    Assert(validRoomName("Caf\xC3\xA9"), "accents");
    Assert(validRoomName("\xF0\x9F\x8E\xAE"), "an emoji");
    Assert(validRoomName(std::string(32, 'x')), "32 bytes is the limit");
    Assert(!validRoomName(std::string(33, 'x')), "33 is too many");
    Assert(!validRoomName(""), "a name is not empty");
    Assert(!validRoomName(" lead"), "no space at the start");
    Assert(!validRoomName("end "), "or at the end");
    Assert(!validRoomName("a\nb"), "no line break");
    Assert(!validRoomName(std::string("a\0b", 3)), "no zero");
    Assert(!validRoomName("a\x7F" "b"), "no DEL");
    Assert(!validRoomName("a\xC2\x85" "b"), "no control character of Latin-1 (U+0085)");
    Assert(!validRoomName("\xFF"), "not UTF-8");
    Assert(!validRoomName("caf\xC3"), "a character that is cut");
    Assert(!validRoomName("\xC0\xAF"), "an overlong form");
    Assert(!validRoomName("\xED\xA0\x80"), "a surrogate");
    Assert(!validRoomName("\xF4\x90\x80\x80"), "past U+10FFFF");
    AssertStrEq(trimRoomName("  hello \t").c_str(), "hello", "the ends are trimmed");
    AssertStrEq(trimRoomName("a b").c_str(), "a b", "not the middle");
    AssertStrEq(trimRoomName("   ").c_str(), "", "nothing left of blanks");
}
