#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! Why a save cannot be used, so that a menu can say something better than "error"
    class SaveError : public std::runtime_error
    {
        public:
            enum class Reason {
                Missing,     //!< No such slot
                Corrupt,     //!< The file is damaged, or is not a save
                WrongGame,   //!< It was written by another game
                TooNew,      //!< Written by a newer version of the game: it cannot know its format
                BadName,     //!< The name of the slot cannot be a file name
                Io,          //!< The disk refused
            };

            SaveError(Reason reason, const std::string& what) : std::runtime_error(what), m_reason(reason) {}

            Reason reason(void) const noexcept { return m_reason; }

        private:
            Reason m_reason;
    };

    //! What a menu shows about a save
    struct SaveInfo
    {
        std::string   slot;
        std::string   label;      //!< A line of text chosen by the game ("Level 3 - 12:04")
        std::uint16_t version;    //!< Of the game that wrote it
        std::uint64_t savedAt;    //!< Seconds since 1970
        std::uint32_t size;       //!< Of the data
    };

    struct SaveData
    {
        SaveInfo                   info;
        std::vector<std::uint8_t>  payload;
    };

    //! One line of a list of saves: a good one, or a damaged one and why
    struct SaveListing
    {
        std::string             slot;
        std::optional<SaveInfo> info;      //!< Nothing if it cannot be read
        std::string             problem;   //!< Then, what is wrong
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The saves of a game: one file per named slot, in a directory
     *
     *     kuge::SaveSlots saves(kuge::userDirectory(kuge::UserDir::Data, "mygame"), "mygame", 3);
     *
     *     saves.write("slot1", "Level 3", writer.bytes());       // a ByteWriter's data
     *     for (const auto& save : saves.list()) { ... }          // to fill a menu
     *     auto data = saves.read("slot1");                       // data.payload
     *
     * A file has the name of the game, the version of the game that wrote it, a
     * label and a checksum, so that a damaged file, a file of another game, or
     * a file from a newer version are told apart and never read as if they were
     * good. A file from an older version is read, and its version is given, so
     * that the game can convert what it holds.
     *
     * Files are written whole or not at all: a crash while saving keeps the
     * previous save.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SaveSlots
    {
        public:
            //! @param clock  Seconds since 1970 (the real time if not given)
            SaveSlots(std::filesystem::path directory, std::string game, std::uint16_t version,
                std::function<std::uint64_t(void)> clock = {});

            //! A slot name is made of letters, digits, '_' and '-', up to 64
            //! @throw SaveError (BadName, Io)
            void write(std::string_view slot, std::string_view label, std::span<const std::uint8_t> payload) const;

            //! @throw SaveError
            SaveData read(std::string_view slot) const;

            bool exists(std::string_view slot) const;

            //! @return  false if there was no such slot
            bool remove(std::string_view slot) const;

            //! Every slot of the directory, by name, damaged ones included
            std::vector<SaveListing> list(void) const;

            const std::filesystem::path& directory(void) const noexcept { return m_directory; }

        private:
            std::filesystem::path pathOf(std::string_view slot) const;

            std::filesystem::path               m_directory;
            std::string                         m_game;
            std::uint16_t                       m_version;
            std::function<std::uint64_t(void)>  m_clock;
    };

    //! A checksum of some bytes (CRC-32)
    std::uint32_t checksum(std::span<const std::uint8_t> bytes) noexcept;

}
