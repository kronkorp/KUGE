#include "Save.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <format>

namespace
{
    constexpr std::uint32_t MAGIC = kuge::fourcc('K', 'S', 'A', 'V');
    constexpr std::uint16_t FORMAT = 1;   // of this container, not of the game's data
    constexpr std::string_view EXTENSION = ".ksave";

    bool validName(std::string_view slot)
    {
        return !slot.empty() && slot.size() <= 64 && std::all_of(slot.begin(), slot.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    }

    std::uint64_t now(void)
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    using Reason = kuge::SaveError::Reason;
}

std::uint32_t kuge::checksum(std::span<const std::uint8_t> bytes) noexcept
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> made{};

        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;

            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
            }
            made[i] = value;
        }
        return made;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;

    for (std::uint8_t byte : bytes) {
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

kuge::SaveSlots::SaveSlots(std::filesystem::path directory, std::string game, std::uint16_t version,
    std::function<std::uint64_t(void)> clock)
    : m_directory(std::move(directory)), m_game(std::move(game)), m_version(version),
      m_clock(clock ? std::move(clock) : std::function<std::uint64_t(void)>(now))
{
}

std::filesystem::path kuge::SaveSlots::pathOf(std::string_view slot) const
{
    if (!validName(slot)) {
        throw SaveError(Reason::BadName, std::format("'{}' cannot be the name of a slot (letters, digits, _ and - only)", slot));
    }
    return m_directory / (std::string(slot) + std::string(EXTENSION));
}

void kuge::SaveSlots::write(std::string_view slot, std::string_view label, std::span<const std::uint8_t> payload) const
{
    const auto path = pathOf(slot);
    ByteWriter out;
    std::error_code error;

    out.writeHeader(MAGIC, FORMAT);
    out.writeString(m_game);
    out.write<std::uint16_t>(m_version);
    out.write<std::uint64_t>(m_clock());
    out.writeString(label);
    out.write<std::uint32_t>(static_cast<std::uint32_t>(payload.size()));
    out.writeBytes(payload);
    out.write<std::uint32_t>(checksum(out.bytes()));

    std::filesystem::create_directories(m_directory, error);
    try {
        writeFile(path, out.bytes());
    } catch (const SerializerError& e) {
        throw SaveError(Reason::Io, e.what());
    }
}

kuge::SaveData kuge::SaveSlots::read(std::string_view slot) const
{
    const auto path = pathOf(slot);
    std::vector<std::uint8_t> bytes;

    if (!std::filesystem::exists(path)) {
        throw SaveError(Reason::Missing, std::format("there is no save '{}'", slot));
    }
    try {
        bytes = readFile(path);
    } catch (const SerializerError& e) {
        throw SaveError(Reason::Io, e.what());
    }
    auto corrupt = [&slot](const std::string& why) {
        return SaveError(Reason::Corrupt, std::format("the save '{}' is damaged: {}", slot, why));
    };
    if (bytes.size() < 4) {
        throw corrupt("too short");
    }
    // The checksum is the last 4 bytes, and covers everything before
    const std::span<const std::uint8_t> body(bytes.data(), bytes.size() - 4);
    ByteReader tail(std::span<const std::uint8_t>(bytes.data() + bytes.size() - 4, 4));

    try {
        ByteReader in(body);
        SaveData data;

        if (in.readHeader(MAGIC) != FORMAT) {
            throw corrupt("unknown file format");
        }
        if (tail.read<std::uint32_t>() != checksum(body)) {
            throw corrupt("the checksum does not match");
        }
        const std::string game = in.readString();
        data.info.slot = std::string(slot);
        data.info.version = in.read<std::uint16_t>();
        data.info.savedAt = in.read<std::uint64_t>();
        data.info.label = in.readString();
        data.info.size = in.read<std::uint32_t>();
        if (game != m_game) {
            throw SaveError(Reason::WrongGame, std::format("the save '{}' is a save of '{}', not of '{}'", slot, game, m_game));
        }
        if (data.info.version > m_version) {
            throw SaveError(Reason::TooNew, std::format("the save '{}' comes from a newer version ({}, this one is {})",
                slot, data.info.version, m_version));
        }
        if (in.remaining() != data.info.size) {
            throw corrupt("the size of the data is wrong");
        }
        const auto payload = in.readBytes(data.info.size);

        data.payload.assign(payload.begin(), payload.end());
        return data;
    } catch (const SerializerError& e) {
        throw corrupt(e.what());
    }
}

bool kuge::SaveSlots::exists(std::string_view slot) const
{
    return std::filesystem::exists(pathOf(slot));
}

bool kuge::SaveSlots::remove(std::string_view slot) const
{
    std::error_code error;

    return std::filesystem::remove(pathOf(slot), error);
}

std::vector<kuge::SaveListing> kuge::SaveSlots::list(void) const
{
    std::vector<SaveListing> found;
    std::error_code error;

    for (const auto& entry : std::filesystem::directory_iterator(m_directory, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != EXTENSION) {
            continue;
        }
        SaveListing listing;

        listing.slot = entry.path().stem().string();
        if (!validName(listing.slot)) {
            continue;
        }
        try {
            listing.info = read(listing.slot).info;
        } catch (const SaveError& e) {
            listing.problem = e.what();
        }
        found.push_back(std::move(listing));
    }
    std::sort(found.begin(), found.end(), [](const SaveListing& a, const SaveListing& b) { return a.slot < b.slot; });
    return found;
}
