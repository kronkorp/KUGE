#pragma once

#include "ui/Font.hpp"

namespace kuge
{

    //! A font whose measures are easy to work out: a letter is 8 pixels wide, a
    //! line is 12 high. The picture is a white block of that size. For tests.
    class DummyFont : public IFont
    {
        public:
            //! `zoom` times as big: scaled() makes one (the nearest whole number), so a test sees text grow
            explicit DummyFont(int zoom = 1) : m_zoom(zoom < 1 ? 1 : zoom) {}

            Vec2  measure(std::string_view text, int wrapWidth = 0) const override;
            Image rasterize(std::string_view text, int wrapWidth = 0) const override;
            int   lineHeight(void) const override { return LINE * m_zoom; }

            static constexpr int LETTER = 8;
            static constexpr int LINE   = 12;

        protected:
            std::shared_ptr<const IFont> resized(float factor) const override
            {
                return std::make_shared<DummyFont>(static_cast<int>(std::lround(static_cast<float>(m_zoom) * factor)));
            }

        private:
            int m_zoom;
    };

    class DummyFontLoader : public IFontLoader
    {
        public:
            //! Any file that exists is a font
            std::shared_ptr<IFont> load(const std::filesystem::path& file, int pointSize) override;

            int loads = 0;
    };

}
