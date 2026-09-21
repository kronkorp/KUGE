#pragma once

#include "ui/Font.hpp"

namespace kuge
{

    //! A font whose measures are easy to work out: a letter is 8 pixels wide, a
    //! line is 12 high. The picture is a white block of that size. For tests.
    class DummyFont : public IFont
    {
        public:
            Vec2  measure(std::string_view text, int wrapWidth = 0) const override;
            Image rasterize(std::string_view text, int wrapWidth = 0) const override;
            int   lineHeight(void) const override { return 12; }

            static constexpr int LETTER = 8;
            static constexpr int LINE   = 12;
    };

    class DummyFontLoader : public IFontLoader
    {
        public:
            //! Any file that exists is a font
            std::shared_ptr<IFont> load(const std::filesystem::path& file, int pointSize) override;

            int loads = 0;
    };

}
