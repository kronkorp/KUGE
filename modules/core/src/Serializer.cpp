#include "Serializer.hpp"
#include <algorithm>
#include <cerrno>
#include <format>
#include <fstream>
#include <system_error>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

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

namespace
{
#ifdef _WIN32
    // Writes all of data to a new file, and asks the disk to keep it
    bool writeSynced(const std::filesystem::path& file, std::span<const std::uint8_t> data)
    {
        const HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        std::size_t done = 0;
        bool written = handle != INVALID_HANDLE_VALUE;

        while (written && done < data.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1u << 30));
            DWORD count = 0;

            written = ::WriteFile(handle, data.data() + done, chunk, &count, nullptr) != 0 && count > 0;
            done += count;
        }
        written = written && ::FlushFileBuffers(handle) != 0;
        return handle != INVALID_HANDLE_VALUE && ::CloseHandle(handle) != 0 && written;
    }

    // Puts the new file in the place of the old one. MOVEFILE_WRITE_THROUGH only returns once
    // the move is on the disk: a power cut cannot bring the old file back.
    void replaceSynced(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
    {
        if (!::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        }
    }
#else
    // Writes all of data to fd, and asks the disk to keep it
    bool writeAndSync(int fd, std::span<const std::uint8_t> data)
    {
        std::size_t done = 0;

        while (done < data.size()) {
            const ssize_t count = ::write(fd, data.data() + done, data.size() - done);

            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            done += static_cast<std::size_t>(count);
        }
        return ::fsync(fd) == 0;
    }

    // The replacement of a file is a change of its folder: without this, a power cut can
    // bring the old file back. Best effort: a folder that cannot be opened (or synced, on a
    // file system that does not allow it) does not undo a save that is already in place.
    void syncFolder(const std::filesystem::path& file)
    {
        std::filesystem::path folder = file.parent_path();

        if (folder.empty()) {
            folder = ".";
        }
        const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY);

        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
    }

    bool writeSynced(const std::filesystem::path& file, std::span<const std::uint8_t> data)
    {
        const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);

        if (fd < 0) {
            return false;
        }
        const bool written = writeAndSync(fd, data);
        const bool closed = ::close(fd) == 0;

        return written && closed;
    }

    void replaceSynced(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
    {
        std::filesystem::rename(from, to, error);
        if (!error) {
            syncFolder(to);
        }
    }
#endif
}

void kuge::writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> data)
{
    std::filesystem::path temporary = path;
    std::error_code error;

    temporary += ".tmp";
    // The data must be on the disk before the rename: otherwise a power cut can leave the
    // new name pointing at a file that is empty or half written
    if (!writeSynced(temporary, data)) {
        std::filesystem::remove(temporary, error);
        throw SerializerError(std::format("cannot write '{}'", temporary.string()));
    }
    replaceSynced(temporary, path, error);
    if (error) {
        const std::string why = error.message();

        std::filesystem::remove(temporary, error);
        throw SerializerError(std::format("cannot replace '{}': {}", path.string(), why));
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
