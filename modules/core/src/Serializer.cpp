#include "Serializer.hpp"
#include <format>
#include <fstream>
#include <system_error>

void kuge::ByteWriter::put(std::uint64_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        m_bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void kuge::ByteWriter::writeCount(std::size_t count)
{
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw SerializerError("ByteWriter: more than 2^32 - 1 elements");
    }
    write(static_cast<std::uint32_t>(count));
}

void kuge::ByteWriter::writeString(std::string_view text)
{
    writeCount(text.size());
    m_bytes.insert(m_bytes.end(), text.begin(), text.end());
}

void kuge::ByteWriter::writeBytes(std::span<const std::uint8_t> bytes)
{
    m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
}

void kuge::ByteWriter::writeHeader(std::uint32_t magic, std::uint16_t version)
{
    write(magic);
    write(version);
}

std::uint64_t kuge::ByteReader::get(std::size_t size)
{
    std::uint64_t value = 0;

    if (size > remaining()) {
        throw SerializerError(std::format(
            "ByteReader: {} byte(s) needed at {}, {} left", size, m_position, remaining()));
    }
    for (std::size_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint64_t>(m_data[m_position + i]) << (8 * i);
    }
    m_position += size;
    return value;
}

// The count comes from the data: check that the elements can fit before
// allocating for them
std::size_t kuge::ByteReader::readCount(std::size_t elementSize)
{
    const std::size_t count = read<std::uint32_t>();

    if (count > remaining() / elementSize) {
        throw SerializerError(std::format(
            "ByteReader: {} element(s) announced, only {} byte(s) left", count, remaining()));
    }
    return count;
}

std::string kuge::ByteReader::readString(void)
{
    const std::size_t size = readCount(1);
    std::string text(reinterpret_cast<const char*>(m_data.data() + m_position), size);

    m_position += size;
    return text;
}

std::span<const std::uint8_t> kuge::ByteReader::readBytes(std::size_t size)
{
    if (size > remaining()) {
        throw SerializerError(std::format(
            "ByteReader: {} byte(s) needed at {}, {} left", size, m_position, remaining()));
    }
    auto bytes = m_data.subspan(m_position, size);

    m_position += size;
    return bytes;
}

std::uint16_t kuge::ByteReader::readHeader(std::uint32_t magic)
{
    const auto found = read<std::uint32_t>();

    if (found != magic) {
        throw SerializerError(std::format(
            "ByteReader: wrong file type (magic {:#010x}, expected {:#010x})", found, magic));
    }
    return read<std::uint16_t>();
}

void kuge::writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> data)
{
    std::filesystem::path temporary = path;
    std::error_code error;

    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);

        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, error);
            throw SerializerError(std::format("cannot write '{}'", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw SerializerError(std::format("cannot replace '{}': {}", path.string(), error.message()));
    }
}

std::vector<std::uint8_t> kuge::readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);

    if (!in) {
        throw SerializerError(std::format("cannot open '{}'", path.string()));
    }
    const std::streamsize size = in.tellg();

    if (size < 0) {
        throw SerializerError(std::format("cannot read '{}'", path.string()));
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));

    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        throw SerializerError(std::format("cannot read '{}'", path.string()));
    }
    return data;
}
