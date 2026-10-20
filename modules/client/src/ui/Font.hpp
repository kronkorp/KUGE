#pragma once

#include "Math2D.hpp"
#include "backend/IRenderer2D.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <map>
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

            //! The same font `factor` times as big (a UI drawn bigger wants sharp text, not a stretched picture). Kept,
            //! so asking every frame is cheap, for as long as this font lives. Called from one thread, the UI's.
            //! nullptr if this font cannot be made again at another size: the caller uses this one.
            std::shared_ptr<const IFont> scaled(float factor) const
            {
                const int key = static_cast<int>(std::lround(factor * 16.0f));   // (a window being dragged asks for many sizes: 1/16 apart)

                if (key <= 0) {
                    return nullptr;
                }
                auto found = m_scaled.find(key);

                if (found == m_scaled.end()) {
                    found = m_scaled.emplace(key, resized(static_cast<float>(key) / 16.0f)).first;
                }
                return found->second;
            }

            //! A number that no other font has, for as long as the process lives:
            //! what a cache uses to tell fonts apart (an address could be used again)
            std::uint64_t id(void) const noexcept { return m_id; }

        protected:
            IFont(void) : m_id(++counter()) {}

            //! The font at `factor` times its size, if it can be: see scaled()
            virtual std::shared_ptr<const IFont> resized(float factor) const { (void)factor; return nullptr; }

        private:
            static std::atomic<std::uint64_t>& counter(void)
            {
                static std::atomic<std::uint64_t> count{0};
                return count;
            }

            std::uint64_t m_id;
            mutable std::map<int, std::shared_ptr<const IFont>> m_scaled;
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
