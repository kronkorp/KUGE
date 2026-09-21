#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kuge
{

    //! Anything wrong while reading or writing binary data: not enough bytes,
    //! wrong file, I/O error...
    class SerializerError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    //! A number, a bool or an enum: what can be written as is
    template<typename T>
    concept Scalar = std::is_arithmetic_v<T> || std::is_enum_v<T>;

    //! Four letters as a number, to identify a kind of file (fourcc('K','S','A','V'))
    constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(a))
            | static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8
            | static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16
            | static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24;
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Writes data in a byte buffer, the same way on every machine
     *
     * Numbers are little-endian with their exact size (an int is 4 bytes, a
     * double 8...), a string is its length as 4 bytes then its characters. The
     * same format is used for the network, the saves and the config: a buffer
     * written here is read by ByteReader whatever the platform.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ByteWriter
    {
        public:
            template<Scalar T>
            void write(T value)
            {
                if constexpr (std::is_enum_v<T>) {
                    write(static_cast<std::underlying_type_t<T>>(value));
                } else if constexpr (std::is_same_v<T, bool>) {
                    put(value ? 1u : 0u, 1);
                } else if constexpr (std::is_floating_point_v<T>) {
                    static_assert(std::numeric_limits<T>::is_iec559
                        && (sizeof(T) == 4 || sizeof(T) == 8),
                        "only float and double can be written");
                    if constexpr (sizeof(T) == 4) {
                        put(std::bit_cast<std::uint32_t>(value), 4);
                    } else {
                        put(std::bit_cast<std::uint64_t>(value), 8);
                    }
                } else {
                    static_assert(sizeof(T) <= 8);
                    put(static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value)), sizeof(T));
                }
            }

            //! @throw SerializerError if it is longer than 4 GiB
            void writeString(std::string_view text);
            void writeBytes(std::span<const std::uint8_t> bytes);

            //! The number of elements as 4 bytes, then the elements
            template<Scalar T>
            void writeVector(const std::vector<T>& values)
            {
                writeCount(values.size());
                for (const T& value : values) {
                    write(value);
                }
            }

            //! What a file starts with: what it is, and the version of its format
            void writeHeader(std::uint32_t magic, std::uint16_t version);

            const std::vector<std::uint8_t>& bytes(void) const noexcept { return m_bytes; }
            std::size_t                      size(void) const noexcept { return m_bytes.size(); }

        private:
            void put(std::uint64_t value, std::size_t size);
            void writeCount(std::size_t count);

            std::vector<std::uint8_t> m_bytes;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Reads what ByteWriter wrote
     *
     * It never trusts the data: reading more than what is left, a string or a
     * vector that claims to be bigger than the buffer, or a bool that is neither
     * 0 nor 1 throws SerializerError instead of reading out of bounds. It does
     * not copy the buffer, which must outlive the reader.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ByteReader
    {
        public:
            explicit ByteReader(std::span<const std::uint8_t> data) noexcept : m_data(data) {}

            template<Scalar T>
            T read(void)
            {
                if constexpr (std::is_enum_v<T>) {
                    return static_cast<T>(read<std::underlying_type_t<T>>());
                } else if constexpr (std::is_same_v<T, bool>) {
                    const std::uint64_t raw = get(1);

                    if (raw > 1) {
                        throw SerializerError("ByteReader: a bool is neither 0 nor 1");
                    }
                    return raw == 1;
                } else if constexpr (std::is_floating_point_v<T>) {
                    static_assert(std::numeric_limits<T>::is_iec559
                        && (sizeof(T) == 4 || sizeof(T) == 8),
                        "only float and double can be read");
                    if constexpr (sizeof(T) == 4) {
                        return std::bit_cast<T>(static_cast<std::uint32_t>(get(4)));
                    } else {
                        return std::bit_cast<T>(get(8));
                    }
                } else {
                    static_assert(sizeof(T) <= 8);
                    return static_cast<T>(static_cast<std::make_unsigned_t<T>>(get(sizeof(T))));
                }
            }

            std::string readString(void);

            //! A view on the next size bytes of the buffer
            std::span<const std::uint8_t> readBytes(std::size_t size);

            template<Scalar T>
            std::vector<T> readVector(void)
            {
                const std::size_t count = readCount(sizeof(T));
                std::vector<T> values;

                values.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    values.push_back(read<T>());
                }
                return values;
            }

            //! @return  The version of the format
            //! @throw   SerializerError if the file does not start with magic
            std::uint16_t readHeader(std::uint32_t magic);

            std::size_t remaining(void) const noexcept { return m_data.size() - m_position; }
            std::size_t position(void) const noexcept { return m_position; }
            bool        atEnd(void) const noexcept { return m_position == m_data.size(); }

        private:
            std::uint64_t get(std::size_t size);
            std::size_t   readCount(std::size_t elementSize);

            std::span<const std::uint8_t> m_data;
            std::size_t                   m_position = 0;
    };

    //! Writes the file whole or not at all: the data goes to a temporary file
    //! next to it, which then replaces it. A crash in the middle of a save
    //! cannot leave half a file.
    //! @throw SerializerError
    void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> data);

    //! @throw SerializerError if the file cannot be read
    std::vector<std::uint8_t> readFile(const std::filesystem::path& path);

}
