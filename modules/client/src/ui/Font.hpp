#pragma once

#include "Math2D.hpp"
#include "backend/IRenderer2D.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace kuge
{

    //! A font that cannot be loaded
    class FontError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A font at a given size: turns text into a picture, and tells how
     *         big it will be
     *
     * Text is UTF-8. A line break in it starts a new line. With a wrap width,
     * lines are also cut at spaces to stay within it (0: no limit).
     */
    ////////////////////////////////////////////////////////////////////////////
    class IFont
    {
        public:
            virtual ~IFont(void) = default;

            //! What the picture of the text would measure, in pixels
            virtual Vec2 measure(std::string_view text, int wrapWidth = 0) const = 0;

            //! White text on a clear background (only the opacity varies), to be
            //! drawn with a tint. Empty for an empty text.
            virtual Image rasterize(std::string_view text, int wrapWidth = 0) const = 0;

            //! The distance between two lines
            virtual int lineHeight(void) const = 0;

            //! A number that no other font has, for as long as the process lives:
            //! what a cache uses to tell fonts apart (an address could be used again)
            std::uint64_t id(void) const noexcept { return m_id; }

        protected:
            IFont(void) : m_id(++counter()) {}

        private:
            static std::atomic<std::uint64_t>& counter(void)
            {
                static std::atomic<std::uint64_t> count{0};
                return count;
            }

            std::uint64_t m_id;
    };

    //! Where fonts come from. A backend gives one (see Backend).
    class IFontLoader
    {
        public:
            virtual ~IFontLoader(void) = default;

            //! @param pointSize  About the height of a letter, in pixels
            //! @throw FontError if the file is not a font
            virtual std::shared_ptr<IFont> load(const std::filesystem::path& file, int pointSize) = 0;
    };

}
